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
#include "AlphaSubtarget.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

static long getUpper16(long l) {
  // ldah adds hi << 16 and the following lda adds a sign-extended lo, so round
  // to the nearest multiple of 65536 (arithmetic shift for negative values).
  return (l + 0x8000) >> 16;
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
  // A Windows EH funclet addresses its parent's frame through $15.
  return MFI.hasVarSizedObjects() ||
         (MF.hasEHFunclets() &&
          MF.getSubtarget().getTargetTriple().isOSWindows());
}

/// The NT unwinder recovers saved registers only from `stq/stt r, n($30)`.
/// Callee-saved slots are therefore always addressed from $30: in the
/// prologue, in a funclet (whose $15 addresses its parent's frame), and in
/// an epilogue, which first moves $15 back into $30.
bool AlphaFrameLowering::isCalleeSaveSlot(const MachineFunction &MF, int FI) {
  for (const CalleeSavedInfo &I : MF.getFrameInfo().getCalleeSavedInfo())
    if (!I.isSpilledToReg() && I.getFrameIdx() == FI)
      return true;
  return false;
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

  // The ELF/OSF ABI enters a function with its procedure value in $27.  Form
  // the function's $gp at every entry for now; later code-size tuning can omit
  // this pair for functions that provably make no GP-relative references.
  const AlphaSubtarget &STI = MF.getSubtarget<AlphaSubtarget>();
  if (STI.getTargetTriple().isOSBinFormatELF()) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAHg), Alpha::R29)
        .addGlobalAddress(&MF.getFunction())
        .addReg(Alpha::R27)
        .addImm(1)
        .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAg), Alpha::R29)
        .addGlobalAddress(&MF.getFunction())
        .addReg(Alpha::R29)
        .addImm(1)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // SEH filters receive the parent's establisher frame, its incoming $30,
  // which is already llvm.localaddress (see lowerWinEHIntrinsic).
  if (STI.getTargetTriple().isOSWindows() && !MBB.isEHFuncletEntry() &&
      MF.hasEHFunclets() &&
      classifyEHPersonality(MF.getFunction().getPersonalityFn()) ==
          EHPersonality::MSVC_TableSEH)
    MF.getWinEHFuncInfo()->SEHSetFrameOffset = 0;

  // Get the number of bytes to allocate from the FrameInfo
  long NumBytes = MFI.getStackSize();

  // The parent's prologue finalizes the frame size, below. A funclet's
  // prologue, emitted afterwards, allocates a frame of that same size.
  if (FP && !MBB.isEHFuncletEntry())
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
  auto EmitAllocation = [&](long Bytes) {
    long Neg = -Bytes;
    if (Neg >= Alpha::IMM_LOW) {
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30)
          .addImm(Neg)
          .addReg(Alpha::R30)
          .setMIFlag(MachineInstr::FrameSetup);
    } else if (getUpper16(Neg) >= Alpha::IMM_LOW) {
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAH), Alpha::R30)
          .addImm(getUpper16(Neg))
          .addReg(Alpha::R30)
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDA), Alpha::R30)
          .addImm(getLower16(Neg))
          .addReg(Alpha::R30)
          .setMIFlag(MachineInstr::FrameSetup);
    } else {
      report_fatal_error("Too big a stack frame at " + Twine(Bytes));
    }
  };

  // The NT unwinder recovers a saved register only from `stq/stt r, n($30)`
  // with a 16-bit n. When the callee-saved area, at the top of the frame, is
  // out of reach of the final $30, allocate it first and save from there, then
  // allocate the rest. NT reverse-executes each `lda/ldah $30` step.
  AlphaMachineFunctionInfo *AFI = MF.getInfo<AlphaMachineFunctionInfo>();
  long SaveArea = 0;
  if (STI.getTargetTriple().isOSWindows() && NumBytes > Alpha::IMM_HIGH) {
    for (const CalleeSavedInfo &CS : MFI.getCalleeSavedInfo())
      if (!CS.isSpilledToReg())
        SaveArea =
            std::max<long>(SaveArea, -MFI.getObjectOffset(CS.getFrameIdx()));
    SaveArea = alignTo(SaveArea, StackAlign);
  }
  AFI->setWinSaveAreaSize(SaveArea);
  if (SaveArea) {
    EmitAllocation(SaveArea);
    // Step over the callee saves, which PEI inserted before emitPrologue.
    while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup))
      ++MBBI;
    EmitAllocation(NumBytes - SaveArea);
  } else {
    EmitAllocation(NumBytes);
  }
  NumBytes = -NumBytes;

  if (STI.getTargetTriple().isOSBinFormatELF() && MF.needsFrameMoves() &&
      NumBytes != 0) {
    unsigned CFIIndex =
        MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, -NumBytes));
    BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Now if we need to, save the old FP and set the new
  const bool IsNT = STI.getTargetTriple().isOSWindows();
  if (FP) {
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::STQ))
        .addReg(Alpha::R15)
        .addImm(0)
        .addReg(Alpha::R30)
        .setMIFlag(MachineInstr::FrameSetup);
    // This must be the last instr in the prolog. On Windows it follows the
    // callee saves, which PEI inserted before emitPrologue, below.
    if (!IsNT)
      BuildMI(MBB, MBBI, dl, TII.get(Alpha::BISr), Alpha::R15)
          .addReg(Alpha::R30)
          .addReg(Alpha::R30)
          .setMIFlag(MachineInstr::FrameSetup);

    if (STI.getTargetTriple().isOSBinFormatELF() && MF.needsFrameMoves()) {
      const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
      unsigned DwarfFP = TRI->getDwarfRegNum(Alpha::R15, true);
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, DwarfFP, NumBytes));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfa(nullptr, DwarfFP, -NumBytes));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  if (STI.getTargetTriple().isOSBinFormatELF() && MF.needsFrameMoves()) {
    const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
    for (const CalleeSavedInfo &CS : MFI.getCalleeSavedInfo()) {
      if (CS.isSpilledToReg())
        continue;
      auto Save = llvm::find_if(
          make_range(MBBI, MBB.end()), [&](const MachineInstr &MI) {
            return MI.mayStore() && MI.getNumOperands() != 0 &&
                   MI.getOperand(0).isReg() &&
                   MI.getOperand(0).getReg() == CS.getReg();
          });
      if (Save == MBB.end())
        continue;
      int64_t Offset = MFI.getObjectOffset(CS.getFrameIdx());
      unsigned DwarfReg = TRI->getDwarfRegNum(CS.getReg(), true);
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
      BuildMI(MBB, std::next(Save), dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }
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

    // NT reverse-executes `bis $30, $30, $15` to recover $30 when the body
    // has moved it (alloca), so the copy must follow every prologue store.
    // A funclet instead points $15 at its parent's frame: the runtime passes
    // the parent's establisher frame (its incoming $30) in $1.
    if (FP && IsNT) {
      if (MBB.isEHFuncletEntry()) {
        MBB.addLiveIn(Alpha::R1);
        int64_t ParentSize = MFI.getStackSize();
        if (-ParentSize >= Alpha::IMM_LOW) {
          BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::LDA), Alpha::R15)
              .addImm(-ParentSize)
              .addReg(Alpha::R1)
              .setMIFlag(MachineInstr::FrameSetup);
        } else {
          BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::LDAH), Alpha::R15)
              .addImm(getUpper16(-ParentSize))
              .addReg(Alpha::R1)
              .setMIFlag(MachineInstr::FrameSetup);
          BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::LDA), Alpha::R15)
              .addImm(getLower16(-ParentSize))
              .addReg(Alpha::R15)
              .setMIFlag(MachineInstr::FrameSetup);
        }
      } else {
        BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::BISr), Alpha::R15)
            .addReg(Alpha::R30)
            .addReg(Alpha::R30)
            .setMIFlag(MachineInstr::FrameSetup);
      }
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
          MBBI->getOpcode() == Alpha::RETDAGp ||
          MBBI->getOpcode() == Alpha::CATCHRET ||
          MBBI->getOpcode() == Alpha::CLEANUPRET) &&
         "Can only insert epilog into returning blocks");
  DebugLoc dl = MBBI->getDebugLoc();

  bool FP = hasFP(MF);
  const bool IsFuncletReturn = MBBI->getOpcode() == Alpha::CATCHRET ||
                               MBBI->getOpcode() == Alpha::CLEANUPRET;

  // Get the number of bytes allocated from the FrameInfo...
  long NumBytes = MFI.getStackSize();

  if (FP && MF.getSubtarget().getTargetTriple().isOSWindows()) {
    // The callee-saved reloads, inserted before emitEpilogue, address their
    // slots from $30. Restore $30 from $15 ahead of them (not in a funclet,
    // whose $15 is its parent's frame). Then reload $15 last, so that NT,
    // which treats every PC before the final deallocation as body code, can
    // still reverse-execute the prologue's `bis $30, $30, $15`.
    if (!IsFuncletReturn) {
      MachineBasicBlock::iterator I = MBBI;
      for (size_t N = MFI.getCalleeSavedInfo().size(); N && I != MBB.begin();
           --N)
        --I;
      BuildMI(MBB, I, dl, TII.get(Alpha::BISr), Alpha::R30)
          .addReg(Alpha::R15)
          .addReg(Alpha::R15);
    }
    if (NumBytes > Alpha::IMM_HIGH)
      emitNTFrameSize(MBB, MBBI, dl, NumBytes);
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDQ), Alpha::R15)
        .addImm(0)
        .addReg(Alpha::R30);
  } else if (FP) {
    //copy the FP into the SP (discards allocas)
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::BISr), Alpha::R30).addReg(Alpha::R15)
      .addReg(Alpha::R15);
    //restore the FP
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDQ), Alpha::R15)
      .addImm(0).addReg(Alpha::R15);
  }

  if (NumBytes > Alpha::IMM_HIGH &&
      MF.getSubtarget().getTargetTriple().isOSWindows()) {
    // NT recognizes an epilogue only at the ret, or at a single `lda $30` or
    // `addq $30, Rx, $30` directly before it.
    if (!FP)
      emitNTFrameSize(MBB, MBBI, dl, NumBytes);
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::ADDQr), Alpha::R30)
        .addReg(Alpha::R30)
        .addReg(Alpha::R28);
  } else if (NumBytes != 0) {
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

void AlphaFrameLowering::emitNTFrameSize(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MBBI,
                                         const DebugLoc &DL,
                                         long NumBytes) const {
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();
  if (getUpper16(NumBytes) > Alpha::IMM_HIGH)
    report_fatal_error("Too big a stack frame at " + Twine(NumBytes));
  BuildMI(MBB, MBBI, DL, TII.get(Alpha::LDAH), Alpha::R28)
      .addImm(getUpper16(NumBytes))
      .addReg(Alpha::R31);
  BuildMI(MBB, MBBI, DL, TII.get(Alpha::LDA), Alpha::R28)
      .addImm(getLower16(NumBytes))
      .addReg(Alpha::R28);
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
