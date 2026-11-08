//===- AlphaRegisterInfo.cpp - Alpha Register Information -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Alpha implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "reginfo"
#include "AlphaRegisterInfo.h"
#include "Alpha.h"
#include "AlphaFrameLowering.h"
#include "AlphaMachineFunctionInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdlib>

#define GET_REGINFO_TARGET_DESC
#include "AlphaGenRegisterInfo.inc"

using namespace llvm;

AlphaRegisterInfo::AlphaRegisterInfo(const TargetInstrInfo &tii)
  : AlphaGenRegisterInfo(Alpha::R26), TII(tii) {
  initLLVMToCVRegMapping(this);
}

static long getUpper16(long l) {
  // ldah adds hi << 16 and the following lda adds a sign-extended lo, so round
  // to the nearest multiple of 65536 (arithmetic shift for negative values).
  return (l + 0x8000) >> 16;
}

static long getLower16(long l) {
  long h = getUpper16(l);
  return l - h * Alpha::IMM_MULT;
}

const MCPhysReg *AlphaRegisterInfo::getCalleeSavedRegs(
    const MachineFunction *MF) const {
  if (MF && MF->getSubtarget().getTargetTriple().isOSWindows())
    return CSR_Alpha_NT_SaveList;
  return CSR_Alpha_SaveList;
}

const uint32_t *AlphaRegisterInfo::getCallPreservedMask(
    const MachineFunction &MF, CallingConv::ID CC) const {
  return CSR_Alpha_RegMask;
}

const uint32_t *AlphaRegisterInfo::getNoPreservedMask() const {
  return CSR_Alpha_NoRegs_RegMask;
}

BitVector AlphaRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(Alpha::R15);
  Reserved.set(Alpha::R28); // AT: frame index elimination scratch
  Reserved.set(Alpha::R29);
  Reserved.set(Alpha::R30);
  Reserved.set(Alpha::R31);
  if (AlphaFrameLowering::hasBP(MF))
    Reserved.set(AlphaFrameLowering::BasePtrReg);
  return Reserved;
}

bool AlphaRegisterInfo::canRealignStack(const MachineFunction &MF) const {
  if (!TargetRegisterInfo::canRealignStack(MF))
    return false;
  // $15 is always reserved. The base pointer must be reservable too, which it
  // no longer is once the reserved registers are frozen without it.
  return !AlphaFrameLowering::needsBasePointerIfRealigned(MF) ||
         MF.getRegInfo().canReserveReg(AlphaFrameLowering::BasePtrReg);
}

//===----------------------------------------------------------------------===//
// Stack Frame Processing methods
//===----------------------------------------------------------------------===//

void AlphaRegisterInfo::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  if (TFI->hasFP(MF)) {
    // If we have a frame pointer, turn the adjcallstackup instruction into a
    // 'sub ESP, <amt>' and the adjcallstackdown instruction into 'add ESP,
    // <amt>'
    MachineInstr *Old = &*I;
    uint64_t Amount = Old->getOperand(0).getImm();
    if (Amount != 0) {
      // We need to keep the stack aligned properly.  To do this, we round the
      // amount of space needed for the outgoing arguments up to the next
      // alignment boundary.
      unsigned StackAlign = TFI->getStackAlign().value();
      Amount = (Amount + StackAlign - 1) / StackAlign * StackAlign;

      MachineInstr *New;
      if (Old->getOpcode() == Alpha::ADJUSTSTACKDOWN) {
        New=BuildMI(MF, Old->getDebugLoc(), TII.get(Alpha::LDA), Alpha::R30)
          .addImm(-Amount).addReg(Alpha::R30);
      } else {
         assert(Old->getOpcode() == Alpha::ADJUSTSTACKUP);
         New=BuildMI(MF, Old->getDebugLoc(), TII.get(Alpha::LDA), Alpha::R30)
          .addImm(Amount).addReg(Alpha::R30);
      }

      // Replace the pseudo instruction with a new instruction...
      MBB.insert(I, New);
    }
  }

  MBB.erase(I);
}

//Alpha has a slightly funny stack:
//Args
//<- incoming SP
//fixed locals (and spills, callee saved, etc)
//<- FP
//variable locals
//<- SP

bool
AlphaRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                       int SPAdj, unsigned FIOperandNum,
                                       RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  unsigned i = FIOperandNum;
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  const auto *TFI = static_cast<const AlphaFrameLowering *>(
      MF.getSubtarget().getFrameLowering());

  while (i >= MI.getNumOperands() || !MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }

  int FrameIndex = MI.getOperand(i).getIndex();

  // LOCAL_ESCAPE names a constant that llvm.localrecover adds to
  // llvm.localaddress, which Windows lowers to the incoming $30, or in a
  // realigned frame to the incoming $30 rounded down to the frame alignment.
  // Either way an escaped object is at its CFA offset from it.
  if (MI.getOpcode() == TargetOpcode::LOCAL_ESCAPE) {
    if (MF.getFrameInfo().isFixedObjectIndex(FrameIndex) &&
        hasStackRealignment(MF))
      report_fatal_error("Cannot escape a fixed object of a realigned frame");
    MI.getOperand(i).ChangeToImmediate(
        MF.getFrameInfo().getObjectOffset(FrameIndex));
    return false;
  }

  // The base register is $30 (SP), $15 (FP, the unaligned frame base), or
  // the aligned frame base ($30 or the base pointer) in a realigned frame.
  // Windows addresses callee-saved slots from $30; see
  // AlphaFrameLowering::isCalleeSaveSlot.
  Register Base = TFI->getFrameBaseRegister(MF, FrameIndex);
  MI.getOperand(i + 1).ChangeToRegister(Base, false);

  // Now add the frame object offset to the offset from the virtual frame index.
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);

  LLVM_DEBUG(errs() << "FI: " << FrameIndex << " Offset: " << Offset << "\n");

  // A callee save in a two-step Windows prologue runs before the second
  // allocation step (see AlphaFrameLowering::emitPrologue).
  long SaveArea = MF.getInfo<AlphaMachineFunctionInfo>()->getWinSaveAreaSize();
  if (SaveArea && MI.getFlag(MachineInstr::FrameSetup) &&
      AlphaFrameLowering::isCalleeSaveSlot(MF, FrameIndex))
    Offset += SaveArea;
  else
    Offset += MF.getFrameInfo().getStackSize();

  LLVM_DEBUG(errs() << "Corrected Offset " << Offset
       << " for stack size: " << MF.getFrameInfo().getStackSize() << "\n");

  if (Offset > Alpha::IMM_HIGH || Offset < Alpha::IMM_LOW) {
    LLVM_DEBUG(errs() << "Unconditionally using R28 for evil purposes Offset: "
          << Offset << "\n");
    //so in this case, we need to use a temporary register, and move the
    //original inst off the SP/FP
    //fix up the old:
    MI.getOperand(i + 1).ChangeToRegister(Alpha::R28, false);
    MI.getOperand(i).ChangeToImmediate(getLower16(Offset));
    //insert the new
    MachineInstr *nMI =
        BuildMI(MF, MI.getDebugLoc(), TII.get(Alpha::LDAH), Alpha::R28)
            .addImm(getUpper16(Offset))
            .addReg(Base);
    MBB.insert(II, nMI);
  } else {
    MI.getOperand(i).ChangeToImmediate(Offset);
  }
  return false;
}

Register AlphaRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  return TFI->hasFP(MF) ? Alpha::R15 : Alpha::R30;
}

unsigned AlphaRegisterInfo::getEHExceptionRegister() const {
  llvm_unreachable("What is the exception register");
  return 0;
}

unsigned AlphaRegisterInfo::getEHHandlerRegister() const {
  llvm_unreachable("What is the exception handler register");
  return 0;
}

std::string AlphaRegisterInfo::getPrettyName(unsigned reg)
{
  std::string s(AlphaRegStrings + AlphaRegDesc[reg].Name);
  return s;
}
