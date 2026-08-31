//=====- AlphaFrameLowering.cpp - Alpha Frame Information ------*- C++ -*-====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Alpha implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "AlphaFrameLowering.h"
#include "AlphaInstrInfo.h"
#include "AlphaSubtarget.h"
#include "AlphaMachineFunctionInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/ADT/Twine.h"

using namespace llvm;

static long getUpper16(long l) {
  long y = l / Alpha::IMM_MULT;
  if (l % Alpha::IMM_MULT > Alpha::IMM_HIGH)
    ++y;
  return y;
}

static long getLower16(long l) {
  long h = getUpper16(l);
  return l - h * Alpha::IMM_MULT;
}

// hasFP - Return true if the specified function should have a dedicated frame
// pointer register.  This is true if the function has variable sized allocas or
// if frame pointer elimination is disabled.
//
bool AlphaFrameLowering::hasFPImpl(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.hasVarSizedObjects();
}

MachineBasicBlock::iterator AlphaFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  MachineInstr &Old = *MI;
  uint64_t Amount = Old.getOperand(0).getImm();

  if (Amount != 0 && !hasReservedCallFrame(MF)) {
    unsigned StackAlign = getStackAlign().value();
    Amount = alignTo(Amount, StackAlign);

    const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
    int64_t Delta = Old.getOpcode() == Alpha::ADJUSTSTACKUP
                        ? -static_cast<int64_t>(Amount)
                        : static_cast<int64_t>(Amount);
    BuildMI(MBB, MI, Old.getDebugLoc(), TII.get(Alpha::LDA), Alpha::R30)
        .addImm(Delta)
        .addReg(Alpha::R30);
  }

  return MBB.erase(MI);
}

void AlphaFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  DebugLoc dl = (MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc());
  bool FP = hasFP(MF);

  // Get the number of bytes to allocate from the FrameInfo
  long NumBytes = MFI.getStackSize();

  if (FP)
    NumBytes += 8; //reserve space for the old FP

  // Leaf functions still need a prologue-end label in their Windows runtime
  // function record, even when the label is the function entry.
  if (NumBytes == 0) {
    if (MF.hasWinCFI())
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::SEH_PrologEnd))
          .setMIFlag(MachineInstr::FrameSetup);
    return;
  }

  unsigned StackAlign = getStackAlign().value();
  NumBytes = (NumBytes+StackAlign-1)/StackAlign*StackAlign;

  // Update frame info to pretend that this is part of the stack...
  MFI.setStackSize(NumBytes);

  // adjust stack pointer: r30 -= numbytes
  NumBytes = -NumBytes;
  if (NumBytes >= Alpha::IMM_LOW) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30)
        .addImm(NumBytes)
        .addReg(Alpha::R30)
        .setMIFlag(MachineInstr::FrameSetup);
  } else if (getUpper16(NumBytes) >= Alpha::IMM_LOW) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAH), Alpha::R30)
        .addImm(getUpper16(NumBytes))
        .addReg(Alpha::R30)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30)
        .addImm(getLower16(NumBytes))
        .addReg(Alpha::R30)
        .setMIFlag(MachineInstr::FrameSetup);
  } else {
    report_fatal_error("Too big a stack frame at " + Twine(NumBytes));
  }

  // Now if we need to, save the old FP and set the new
  if (FP) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::STQ))
        .addReg(Alpha::R15)
        .addImm(0)
        .addReg(Alpha::R30)
        .setMIFlag(MachineInstr::FrameSetup);
    // This must be the last instr in the prolog
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::BISr), Alpha::R15)
        .addReg(Alpha::R30)
        .addReg(Alpha::R30)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  if (MF.hasWinCFI()) {
    // The NT Alpha unwinder decodes the prologue instructions themselves.
    // Include the callee-save stores that PEI inserted before emitPrologue.
    MachineBasicBlock::iterator PrologEnd = MBB.begin();
    while (PrologEnd != MBB.end() &&
           PrologEnd->getFlag(MachineInstr::FrameSetup))
      ++PrologEnd;

    // WinEH may save the return address directly without representing it as
    // callee-saved information.  This is one of the canonical prologue forms
    // decoded by the NT Alpha unwinder.
    while (PrologEnd != MBB.end() && PrologEnd->getOpcode() == Alpha::STQ &&
           PrologEnd->getOperand(0).isReg() &&
           PrologEnd->getOperand(0).getReg() == Alpha::R26) {
      PrologEnd->setFlag(MachineInstr::FrameSetup);
      ++PrologEnd;
    }

    BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::SEH_PrologEnd))
        .setMIFlag(MachineInstr::FrameSetup);
  }
}

void AlphaFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *) const {
  const AlphaSubtarget &STI = MF.getSubtarget<AlphaSubtarget>();
  if (STI.getTargetTriple().isOSWindows() &&
      (MF.getFunction().needsUnwindTableEntry() || MF.hasEHFunclets()))
    MF.setHasWinCFI(true);
}

void AlphaFrameLowering::emitEpilogue(MachineFunction &MF,
                                  MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  assert((MBBI->getOpcode() == Alpha::RETDAG ||
          MBBI->getOpcode() == Alpha::RETDAGp)
         && "Can only insert epilog into returning blocks");
  DebugLoc dl = MBBI->getDebugLoc();

  bool FP = hasFP(MF);

  // Get the number of bytes allocated from the FrameInfo...
  long NumBytes = MFI.getStackSize();

  //now if we need to, restore the old FP
  if (FP) {
    //copy the FP into the SP (discards allocas)
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::BISr), Alpha::R30).addReg(Alpha::R15)
      .addReg(Alpha::R15);
    //restore the FP
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDQ), Alpha::R15)
      .addImm(0).addReg(Alpha::R15);
  }

  if (NumBytes != 0) {
    if (NumBytes <= Alpha::IMM_HIGH) {
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30).addImm(NumBytes)
        .addReg(Alpha::R30);
    } else if (getUpper16(NumBytes) <= Alpha::IMM_HIGH) {
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAH), Alpha::R30)
        .addImm(getUpper16(NumBytes)).addReg(Alpha::R30);
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30)
        .addImm(getLower16(NumBytes)).addReg(Alpha::R30);
    } else {
      report_fatal_error("Too big a stack frame at " + Twine(NumBytes));
    }
  }
}

bool AlphaFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI,
    const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();

  for (const CalleeSavedInfo &CS : CSI) {
    MCRegister Reg = CS.getReg();
    if (CS.isSpilledToReg()) {
      BuildMI(MBB, MI, DebugLoc(), TII.get(TargetOpcode::COPY), CS.getDstReg())
          .addReg(Reg, getKillRegState(true))
          .setMIFlag(MachineInstr::FrameSetup);
      continue;
    }

    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.storeRegToStackSlot(MBB, MI, Reg, true, CS.getFrameIdx(), RC,
                            Register(), MachineInstr::FrameSetup);
  }
  return true;
}

bool AlphaFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI,
    const TargetRegisterInfo *TRI) const {
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();

  for (size_t I = CSI.size(); I != 0; --I) {
    const CalleeSavedInfo &CS = CSI[I - 1];
    MCRegister Reg = CS.getReg();
    if (CS.isSpilledToReg()) {
      BuildMI(MBB, MI, DebugLoc(), TII.get(TargetOpcode::COPY), Reg)
          .addReg(CS.getDstReg(), getKillRegState(true))
          .setMIFlag(MachineInstr::FrameDestroy);
      continue;
    }

    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.loadRegFromStackSlot(MBB, MI, Reg, CS.getFrameIdx(), RC, Register(),
                            MachineInstr::FrameDestroy);
  }
  return true;
}
