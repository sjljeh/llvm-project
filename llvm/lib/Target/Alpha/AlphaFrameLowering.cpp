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

  // Do we need to allocate space on the stack?
  if (NumBytes == 0) return;

  unsigned StackAlign = getStackAlign().value();
  NumBytes = (NumBytes+StackAlign-1)/StackAlign*StackAlign;

  // Update frame info to pretend that this is part of the stack...
  MFI.setStackSize(NumBytes);

  // adjust stack pointer: r30 -= numbytes
  NumBytes = -NumBytes;
  if (NumBytes >= Alpha::IMM_LOW) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30).addImm(NumBytes)
      .addReg(Alpha::R30);
  } else if (getUpper16(NumBytes) >= Alpha::IMM_LOW) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAH), Alpha::R30)
      .addImm(getUpper16(NumBytes)).addReg(Alpha::R30);
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30)
      .addImm(getLower16(NumBytes)).addReg(Alpha::R30);
  } else {
    report_fatal_error("Too big a stack frame at " + Twine(NumBytes));
  }

  // Now if we need to, save the old FP and set the new
  if (FP) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::STQ))
      .addReg(Alpha::R15).addImm(0).addReg(Alpha::R30);
    // This must be the last instr in the prolog
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::BISr), Alpha::R15)
      .addReg(Alpha::R30).addReg(Alpha::R30);
  }

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
