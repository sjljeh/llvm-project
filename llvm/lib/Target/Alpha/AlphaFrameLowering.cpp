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
#include "AlphaRegisterInfo.h"
#include "AlphaSubtarget.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
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
  // A Windows EH funclet addresses its parent's frame through $15. A
  // realigned frame keeps its unaligned $30 in $15 for the epilogue and for
  // the NT unwinder, which reverse-executes `bis $30, $30, $15`.
  return MFI.hasVarSizedObjects() ||
         (MF.hasEHFunclets() &&
          MF.getSubtarget().getTargetTriple().isOSWindows()) ||
         MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
}

// Stack realignment.
//
// The prologue allocates the frame, saves registers and sets $15 = $30 as
// before; $15 (the "unaligned base", CFA - StackSize) is what the epilogue
// moves back into $30 and what NT reverse-executes `bis $30, $30, $15` into.
// It then rounds $15 down to the maximum alignment A with `bic` or `and`,
// which the NT unwinder ignores (of the logical operations it reverses only
// `bis`), into $30 and, when needed, into the base pointer $14.
//
// The frame size is a multiple of A, so the aligned base plus
// StackSize + ObjectOffset is A-aligned whenever ObjectOffset is. Objects at
// fixed CFA offsets are addressed from $15 (or $30 in the prologue and
// epilogue): callee-save slots, incoming arguments, and the objects the NT
// C++ runtime finds from the establisher frame (the EH context record and catch
// objects, which processFunctionBeforeFrameFinalized pins to fixed slots).
// Every other object moves down with the aligned base, by less than A, and
// stays above the unaligned $30.
//
// A funclet recomputes the aligned base from its parent's $15 with the same
// instruction. Windows llvm.localaddress is the aligned virtual frame,
// (CFA & -A) = aligned base + StackSize, so that escaped offsets stay CFA
// offsets and llvm.eh.recoverfp is EstablisherFrame & -A.
MCSymbol *llvm::getAlphaParentFrameMaskSymbol(MCContext &Ctx,
                                              StringRef FuncName) {
  return Ctx.getOrCreateSymbol(
      Twine(Ctx.getAsmInfo().getInternalSymbolPrefix()) + FuncName +
      "$parent_frame_mask");
}

static bool isRealigned(const MachineFunction &MF) {
  return MF.getSubtarget().getRegisterInfo()->hasStackRealignment(MF);
}

bool AlphaFrameLowering::needsBasePointerIfRealigned(
    const MachineFunction &MF) {
  return MF.getFrameInfo().hasVarSizedObjects() ||
         (MF.hasEHFunclets() &&
          MF.getSubtarget().getTargetTriple().isOSWindows());
}

bool AlphaFrameLowering::hasBP(const MachineFunction &MF) {
  return isRealigned(MF) && needsBasePointerIfRealigned(MF);
}

// A realigned frame without a base pointer addresses its objects from the
// aligned $30, so $30 must not move around calls.
// NTUW rules describe a prologue anywhere in the function; NT 4.0 tables
// require it at the entry.
bool AlphaFrameLowering::enableShrinkWrapping(const MachineFunction &MF) const {
  return MF.getSubtarget().getTargetTriple().usesNTRISCTableUnwind();
}

bool AlphaFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !hasFP(MF) || (isRealigned(MF) && !hasBP(MF));
}

void AlphaFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  if (hasBP(MF))
    SavedRegs.set(BasePtrReg);
}

int64_t AlphaFrameLowering::getFPSaveOffset(const MachineFunction &MF) const {
  // With a reserved call frame (a realigned frame without a base pointer),
  // outgoing arguments start at the aligned $30, up to A - 16 bytes below
  // the allocated $30, so they may cover its first bytes. Save $15 just above
  // the outgoing argument area instead; realigned objects start higher.
  if (!hasReservedCallFrame(MF))
    return 0;
  return alignTo(MF.getFrameInfo().getMaxCallFrameSize(), 8);
}

Register AlphaFrameLowering::getFrameBaseRegister(const MachineFunction &MF,
                                                  int FI) const {
  if (!hasFP(MF))
    return Alpha::R30;
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (isCalleeSaveSlot(MF, FI))
    return MF.getSubtarget().getTargetTriple().isOSWindows() ? Alpha::R30
                                                             : Alpha::R15;
  // EH context record: see pinRuntimeFrameObjects.
  const WinEHFuncInfo *EHInfo = MF.getWinEHFuncInfo();
  if (EHInfo && FI == EHInfo->EHRegNodeFrameIndex)
    return Alpha::R15;
  if (isRealigned(MF) &&
      (!MFI.isFixedObjectIndex(FI) ||
       FI == MF.getInfo<AlphaMachineFunctionInfo>()->getWinEHLocalAddressFI()))
    return hasBP(MF) ? Register(BasePtrReg) : Register(Alpha::R30);
  return Alpha::R15;
}

StackOffset
AlphaFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                           Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  FrameReg = getFrameBaseRegister(MF, FI);
  return StackOffset::getFixed(MFI.getObjectOffset(FI) + MFI.getStackSize());
}

void AlphaFrameLowering::emitAlignBase(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       const DebugLoc &DL, Register Src,
                                       Register Dst, uint64_t Align) const {
  const TargetInstrInfo &TII = *MBB.getParent()->getSubtarget().getInstrInfo();
  if (Align - 1 <= 255) {
    BuildMI(MBB, MBBI, DL, TII.get(Alpha::BICi), Dst)
        .addReg(Src)
        .addImm(Align - 1)
        .setMIFlag(MachineInstr::FrameSetup);
    return;
  }
  // $28 = -Align. NT reverses an lda/ldah only when it writes $30 or the
  // register of a pending `subq $30, Rx, $30`.
  int64_t Mask = -static_cast<int64_t>(Align);
  if (isInt<16>(Mask)) {
    BuildMI(MBB, MBBI, DL, TII.get(Alpha::LDA), Alpha::R28)
        .addImm(Mask)
        .addReg(Alpha::R31)
        .setMIFlag(MachineInstr::FrameSetup);
  } else {
    if (getUpper16(Mask) < Alpha::IMM_LOW || getLower16(Mask) != 0)
      report_fatal_error("Unsupported Alpha stack alignment " + Twine(Align));
    BuildMI(MBB, MBBI, DL, TII.get(Alpha::LDAH), Alpha::R28)
        .addImm(getUpper16(Mask))
        .addReg(Alpha::R31)
        .setMIFlag(MachineInstr::FrameSetup);
  }
  BuildMI(MBB, MBBI, DL, TII.get(Alpha::ANDr), Dst)
      .addReg(Src)
      .addReg(Alpha::R28)
      .setMIFlag(MachineInstr::FrameSetup);
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

  // A realigned frame's size is a multiple of its alignment (see isRealigned).
  // Rounding $15 down to it moves $30 by up to MaxAlign - StackAlign more.
  const bool Realign = isRealigned(MF);
  const uint64_t MaxAlign = MFI.getMaxAlign().value();
  long RealignSlack = 0;
  if (Realign && !MBB.isEHFuncletEntry()) {
    NumBytes = alignTo(NumBytes, MaxAlign);
    RealignSlack = MaxAlign - StackAlign;
  }

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

  // NT commits a stack only through the guard page below it, so a frame
  // larger than a probe interval touches each of its pages before $30
  // moves. inlineStackProbe expands this marker, placed
  // directly before the allocation step that crosses the pages.
  const AlphaTargetLowering &TLI = *STI.getTargetLowering();
  const bool Probe =
      TLI.hasInlineStackProbe(MF) &&
      NumBytes + RealignSlack > static_cast<long>(TLI.getStackProbeSize(MF));
  // An `ldah $30` step rounds to the nearest 64 KB, so it can take $30 below
  // the final value before the `lda $30` that follows moves it back up. Probe
  // down to that lowest point, and to the lowest $30 that realignment can
  // produce.
  auto EmitProbe = [&](long Current) {
    long Lowest = NumBytes + RealignSlack;
    long Neg = Current - NumBytes;
    if (Neg < Alpha::IMM_LOW)
      Lowest = std::max(Lowest, Current - getUpper16(Neg) * Alpha::IMM_MULT);
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::NT_PROBE_STACK))
        .addImm(Current)
        .addImm(Lowest)
        .setMIFlag(MachineInstr::FrameSetup);
  };

  if (SaveArea) {
    EmitAllocation(SaveArea);
    // Step over the callee saves, which PEI inserted before emitPrologue.
    while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup))
      ++MBBI;
    if (Probe)
      EmitProbe(SaveArea);
    EmitAllocation(NumBytes - SaveArea);
  } else {
    if (Probe)
      EmitProbe(0);
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
    int64_t FPSaveOffset = getFPSaveOffset(MF);
    if (!isInt<16>(FPSaveOffset))
      report_fatal_error("Too big an outgoing argument area");
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::STQ))
        .addReg(Alpha::R15)
        .addImm(FPSaveOffset)
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

  if (Realign && FP && !IsNT) {
    MachineBasicBlock::iterator I = MBB.begin();
    while (I != MBB.end() && I->getFlag(MachineInstr::FrameSetup))
      ++I;
    emitAlignBase(MBB, I, dl, Alpha::R15, Alpha::R30, MaxAlign);
    if (hasBP(MF))
      emitAlignBase(MBB, I, dl, Alpha::R15, BasePtrReg, MaxAlign);
  }

  // A Windows frame pointer is set after the register saves, even without
  // unwind information, since the epilogue restores $30 from it.
  if (MF.hasWinCFI() || (FP && IsNT)) {
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
    // A funclet instead points $15 at its parent's frame. The C SEH runtime
    // passes the parent's establisher frame (its incoming $30) in $1. The C++
    // runtime passes, also in $1, the real frame pointer that the parent
    // recorded in its EH context record, which is the parent's $15.
    if (FP && IsNT) {
      if (MBB.isEHFuncletEntry() && isAlphaWinCXXEH(MF)) {
        MBB.addLiveIn(Alpha::R1);
        BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::BISr), Alpha::R15)
            .addReg(Alpha::R1)
            .addReg(Alpha::R1)
            .setMIFlag(MachineInstr::FrameSetup);
      } else if (MBB.isEHFuncletEntry()) {
        // NTUW passes the establisher frame as the second argument.
        const unsigned Establisher =
            STI.getTargetTriple().usesNTRISCTableUnwind() ? Alpha::R17
                                                          : Alpha::R1;
        MBB.addLiveIn(Establisher);
        int64_t ParentSize = MFI.getStackSize();
        // Save the establisher frame where __CxxFrameHandler3 looks for it.
        const WinEHFuncInfo *EHInfo = MF.getWinEHFuncInfo();
        if (EHInfo &&
            EHInfo->FuncletParentFrameIdx != std::numeric_limits<int>::max()) {
          int64_t Slot =
              MFI.getObjectOffset(EHInfo->FuncletParentFrameIdx) + ParentSize;
          Register Base = Alpha::R30;
          if (!isInt<16>(Slot)) {
            BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::LDAH), Alpha::R28)
                .addImm(getUpper16(Slot))
                .addReg(Alpha::R30)
                .setMIFlag(MachineInstr::FrameSetup);
            Base = Alpha::R28;
            Slot = getLower16(Slot);
          }
          BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::STQ))
              .addReg(Establisher)
              .addImm(Slot)
              .addReg(Base)
              .setMIFlag(MachineInstr::FrameSetup);
        }
        if (-ParentSize >= Alpha::IMM_LOW) {
          BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::LDA), Alpha::R15)
              .addImm(-ParentSize)
              .addReg(Establisher)
              .setMIFlag(MachineInstr::FrameSetup);
        } else {
          BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::LDAH), Alpha::R15)
              .addImm(getUpper16(-ParentSize))
              .addReg(Establisher)
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

      // Realign after the `bis $30, $30, $15` that NT reverse-executes, with
      // `bic`/`and`, which it ignores, so that every PC up to and including
      // PrologEnd unwinds through $15. A funclet recomputes its parent's
      // aligned base from the parent's $15 the same way.
      if (Realign) {
        if (!MBB.isEHFuncletEntry())
          emitAlignBase(MBB, PrologEnd, dl, Alpha::R15, Alpha::R30, MaxAlign);
        if (hasBP(MF))
          emitAlignBase(MBB, PrologEnd, dl, Alpha::R15, BasePtrReg, MaxAlign);
      }
    }

    if (MF.hasWinCFI())
      BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::SEH_PrologEnd))
          .setMIFlag(MachineInstr::FrameSetup);

    // the EH context record's Rfp = $15, the frame base that C++ funclets
    // receive in $1. The handler ignores PCs before PrologEnd, so this is body
    // code.
    const WinEHFuncInfo *EHInfo = MF.getWinEHFuncInfo();
    if (FP && IsNT && !MBB.isEHFuncletEntry() && EHInfo &&
        EHInfo->EHRegNodeFrameIndex != std::numeric_limits<int>::max()) {
      int64_t Off = MFI.getObjectOffset(EHInfo->EHRegNodeFrameIndex) +
                    MFI.getStackSize() + 4;
      Register Base = Alpha::R15;
      if (Off < Alpha::IMM_LOW || Off > Alpha::IMM_HIGH) {
        BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::LDAH), Alpha::R28)
            .addImm(getUpper16(Off))
            .addReg(Alpha::R15);
        Off = getLower16(Off);
        Base = Alpha::R28;
      }
      BuildMI(MBB, PrologEnd, dl, TII.get(Alpha::STL))
          .addReg(Alpha::R15)
          .addImm(Off)
          .addReg(Base);
    }
  }
}

// Expand NT_PROBE_STACK, which sits directly before the allocation step that
// moves $30 from CFA - Cur down to at least CFA - Final (CFA = incoming $30).
// Store $31 at CFA - k * PS for k = 1..N, N = (Final - 1) / PS, then at
// CFA - Final, so that no store is a page or more below the one before it and
// the page holding the new $30 is committed, as Windows' own stack check
// does.
//
// The NT unwinder reverse-executes every prologue instruction. It skips `stq
// $31, n($30)` (it reloads only Ra != $31) and stores through other bases, and
// it only reverses an `lda/ldah` that writes $30 or the register of a pending
// `subq $30, Rx, $30` or `lda $30, n(Rx)`. The probes write only $28 (reserved)
// and the count $22, with lda/ldah/subq-literal, none of which it reverses.
void AlphaFrameLowering::inlineStackProbe(MachineFunction &MF,
                                          MachineBasicBlock &MBB) const {
  auto Where = llvm::find_if(MBB, [](const MachineInstr &MI) {
    return MI.getOpcode() == Alpha::NT_PROBE_STACK;
  });
  if (Where == MBB.end())
    return;
  MachineInstr &MI = *Where;
  const AlphaSubtarget &STI = MF.getSubtarget<AlphaSubtarget>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const AlphaTargetLowering &TLI = *STI.getTargetLowering();
  const Register SP = Alpha::R30, Ptr = Alpha::R28, Count = Alpha::R22;
  DebugLoc DL = MI.getDebugLoc();
  MachineBasicBlock::iterator MBBI = MI;
  int64_t Cur = MI.getOperand(0).getImm();
  int64_t Final = MI.getOperand(1).getImm();
  int64_t PS = TLI.getStackProbeSize(MF);
  int64_t N = (Final - 1) / PS;
  assert(N > 0 && isInt<16>(Cur - PS) && "Unexpected probed frame");

  auto Flag = [](MachineInstrBuilder MIB) {
    return MIB.setMIFlag(MachineInstr::FrameSetup);
  };
  // The implicit $30 use keeps each store ahead of the allocation.
  auto Store = [&](MachineBasicBlock &B, MachineBasicBlock::iterator I,
                   int64_t Off, Register Base) {
    MachineInstrBuilder MIB = BuildMI(B, I, DL, TII.get(Alpha::STQ))
                                  .addReg(Alpha::R31)
                                  .addImm(Off)
                                  .addReg(Base);
    if (Base != SP)
      MIB.addReg(SP, RegState::Implicit);
    Flag(MIB);
  };
  auto Lda = [&](MachineBasicBlock &B, MachineBasicBlock::iterator I,
                 unsigned Opc, Register Dst, int64_t Imm, Register Base) {
    Flag(BuildMI(B, I, DL, TII.get(Opc), Dst).addImm(Imm).addReg(Base));
  };

  if (N <= 4 && isInt<16>(Cur - Final)) {
    for (int64_t K = 1; K <= N; ++K)
      Store(MBB, MBBI, Cur - K * PS, SP);
    Store(MBB, MBBI, Cur - Final, SP);
    MI.eraseFromParent();
    return;
  }

  // $28 = CFA - PS.
  Lda(MBB, MBBI, Alpha::LDA, Ptr, Cur - PS, SP);
  if (N <= 4) {
    Store(MBB, MBBI, 0, Ptr);
    for (int64_t K = 2; K <= N; ++K) {
      Lda(MBB, MBBI, Alpha::LDA, Ptr, -PS, Ptr);
      Store(MBB, MBBI, 0, Ptr);
    }
    // $28 = CFA - N * PS, and N * PS < Final <= (N + 1) * PS.
    Store(MBB, MBBI, N * PS - Final, Ptr);
    MI.eraseFromParent();
    return;
  }

  if (isInt<16>(N)) {
    Lda(MBB, MBBI, Alpha::LDA, Count, N, Alpha::R31);
  } else {
    Lda(MBB, MBBI, Alpha::LDAH, Count, getUpper16(N), Alpha::R31);
    Lda(MBB, MBBI, Alpha::LDA, Count, getLower16(N), Count);
  }

  // loop: stq $31, 0($28); subq $22, 1, $22; lda $28, -PS($28); bne $22, loop
  const BasicBlock *BB = MBB.getBasicBlock();
  MachineFunction::iterator InsertPt = std::next(MBB.getIterator());
  MachineBasicBlock *LoopMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(InsertPt, LoopMBB);
  MachineBasicBlock *ExitMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(InsertPt, ExitMBB);
  Store(*LoopMBB, LoopMBB->end(), 0, Ptr);
  Flag(BuildMI(LoopMBB, DL, TII.get(Alpha::SUBQi), Count)
           .addReg(Count)
           .addImm(1));
  Lda(*LoopMBB, LoopMBB->end(), Alpha::LDA, Ptr, -PS, Ptr);
  Flag(BuildMI(LoopMBB, DL, TII.get(Alpha::COND_BRANCH_I))
           .addImm(Alpha::BNE)
           .addReg(Count)
           .addMBB(LoopMBB));
  LoopMBB->addSuccessor(LoopMBB);
  LoopMBB->addSuccessor(ExitMBB);
  ExitMBB->splice(ExitMBB->end(), &MBB, std::next(MBBI), MBB.end());
  ExitMBB->transferSuccessorsAndUpdatePHIs(&MBB);
  MBB.addSuccessor(LoopMBB);
  // $28 = CFA - (N + 1) * PS.
  Store(*ExitMBB, ExitMBB->begin(), (N + 1) * PS - Final, Ptr);
  MI.eraseFromParent();
  fullyRecomputeLiveIns({ExitMBB, LoopMBB});
}

/// Whether \p MF uses the NT Alpha C++ runtime, __CxxFrameHandler.
bool AlphaFrameLowering::isAlphaWinCXXEH(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  if (!MF.getSubtarget().getTargetTriple().isOSWindows() ||
      !F.hasPersonalityFn())
    return false;
  const auto *PerFn =
      dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
  return PerFn && PerFn->getName() == "__CxxFrameHandler" &&
         classifyEHPersonality(PerFn) == EHPersonality::MSVC_CXX;
}

void AlphaFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *) const {
  const AlphaSubtarget &STI = MF.getSubtarget<AlphaSubtarget>();
  if (STI.getTargetTriple().isOSWindows() &&
      (MF.getFunction().needsUnwindTableEntry() || MF.hasEHFunclets()))
    MF.setHasWinCFI(true);

  // Every return reads $26 again, now that the callee-saved restores are in
  // place (see AlphaTargetLowering::finalizeLowering).
  if (STI.getTargetTriple().usesNTRISCTableUnwind())
    for (MachineBasicBlock &MBB : MF)
      for (MachineInstr &MI : MBB.terminators())
        if ((MI.getOpcode() == Alpha::RETDAGp ||
             MI.getOpcode() == Alpha::RETDAG) &&
            !MI.readsRegister(Alpha::R26, /*TRI=*/nullptr))
          MI.addOperand(MF, MachineOperand::CreateReg(Alpha::R26,
                                                      /*isDef=*/false,
                                                      /*isImp=*/true));

  // Find the EH context record (AlphaWinEHState). PEI places it first, at the
  // top of the frame, and WinException emits its offset as EHContextDelta.
  WinEHFuncInfo *EHInfo = MF.getWinEHFuncInfo();
  if (EHInfo && isAlphaWinCXXEH(MF)) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    for (int FI = 0, E = MFI.getObjectIndexEnd(); FI != E; ++FI) {
      const AllocaInst *AI = MFI.getObjectAllocation(FI);
      if (AI && !MFI.isDeadObjectIndex(FI) && isAlphaWinCXXEHContext(*AI)) {
        EHInfo->EHRegNodeFrameIndex = FI;
        break;
      }
    }
  }

  if (EHInfo && MF.hasEHFunclets() && isNTUWCXXEH(MF))
    createNTUWCXXEHObjects(MF, *EHInfo);

  if (EHInfo && STI.getTargetTriple().isOSWindows() && isRealigned(MF))
    pinRuntimeFrameObjects(MF, *EHInfo);
}

bool AlphaFrameLowering::isNTUWCXXEH(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  if (!MF.getSubtarget().getTargetTriple().usesNTRISCTableUnwind() ||
      !F.hasPersonalityFn())
    return false;
  const auto *PerFn =
      dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
  return PerFn && PerFn->getName() == "__CxxFrameHandler3";
}

/// NTUW's __CxxFrameHandler3 reads two words at fixed offsets from an
/// establisher frame (the CFA): UnwindHelp, the try-block state of a parent
/// being unwound, which starts as -2; and, in a catch funclet's frame, the
/// establisher frame the funclet was called with. Funclet frames have the
/// parent's layout, so one fixed object serves every funclet.
void AlphaFrameLowering::createNTUWCXXEHObjects(MachineFunction &MF,
                                                WinEHFuncInfo &EHInfo) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t Top = 0;
  for (int FI = MFI.getObjectIndexBegin(); FI != 0; ++FI)
    if (!MFI.isDeadObjectIndex(FI))
      Top = std::max(Top, -MFI.getObjectOffset(FI));
  Top = alignTo(Top + 8, 8);
  EHInfo.FuncletParentFrameIdx =
      MFI.CreateFixedObject(8, -Top, /*IsImmutable=*/false);
  Top += 8;
  int UnwindHelp = MFI.CreateFixedObject(8, -Top, /*IsImmutable=*/false);
  EHInfo.UnwindHelpFrameIdx = UnwindHelp;

  MachineBasicBlock &Entry = MF.front();
  MachineBasicBlock::iterator I = Entry.begin();
  while (I != Entry.end() && I->getFlag(MachineInstr::FrameSetup))
    ++I;
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  BuildMI(Entry, I, DebugLoc(), TII.get(Alpha::LDA), Alpha::R28)
      .addImm(-2)
      .addReg(Alpha::R31);
  BuildMI(Entry, I, DebugLoc(), TII.get(Alpha::STL))
      .addReg(Alpha::R28, RegState::Kill)
      .addFrameIndex(UnwindHelp)
      .addReg(Alpha::F31);
}

/// The NT C++ runtime finds catch objects at fixed offsets from the
/// establisher frame, the CFA. In a realigned frame,
/// ordinary objects move with the aligned base, so move catch objects into
/// fixed objects at the top of the frame, addressed from $15 like incoming
/// arguments. (The EH context record, found at EHContextDelta from the same
/// frame, is addressed from $15 too; PEI places it above every other local.)
void AlphaFrameLowering::pinRuntimeFrameObjects(MachineFunction &MF,
                                                WinEHFuncInfo &EHInfo) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<int *, 8> Refs;
  for (WinEHTryBlockMapEntry &TBME : EHInfo.TryBlockMap)
    for (WinEHHandlerType &H : TBME.HandlerArray)
      if (H.CatchObj.FrameIndex != std::numeric_limits<int>::max())
        Refs.push_back(&H.CatchObj.FrameIndex);
  if (Refs.empty())
    return;

  // The fixed area below the CFA (llvm.localaddress sits at offset 0).
  int64_t Top = 0;
  for (int FI = MFI.getObjectIndexBegin(); FI != 0; ++FI)
    if (!MFI.isDeadObjectIndex(FI))
      Top = std::max(Top, -MFI.getObjectOffset(FI));

  DenseMap<int, int> Pinned;
  for (int *Ref : Refs) {
    int Old = *Ref;
    if (MFI.isFixedObjectIndex(Old))
      continue;
    auto [It, Inserted] = Pinned.try_emplace(Old, 0);
    if (Inserted) {
      Align ObjAlign = MFI.getObjectAlign(Old);
      if (ObjAlign > getStackAlign()) {
        // The establisher frame is only StackAlign-aligned.
        MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported(
            MF.getFunction(),
            "Windows Alpha C++ EH cannot place an object aligned to more "
            "than " +
                Twine(getStackAlign().value()) +
                " bytes at a fixed offset from the establisher frame "
                "(over-aligned catch object)"));
        ObjAlign = getStackAlign();
      }
      int64_t Size = MFI.getObjectSize(Old);
      Top = alignTo(Top + Size, ObjAlign);
      It->second = MFI.CreateFixedObject(Size, -Top, /*IsImmutable=*/false,
                                         /*isAliased=*/true);
    }
    *Ref = It->second;
  }
  if (Pinned.empty())
    return;

  for (MachineBasicBlock &MBB : MF)
    for (MachineInstr &MI : MBB)
      for (MachineOperand &MO : MI.operands())
        if (MO.isFI())
          if (auto It = Pinned.find(MO.getIndex()); It != Pinned.end())
            MO.setIndex(It->second);
  for (MachineFunction::VariableDbgInfo &VI : MF.getVariableDbgInfo())
    if (VI.inStackSlot())
      if (auto It = Pinned.find(VI.getStackSlot()); It != Pinned.end())
        VI.updateStackSlot(It->second);
  for (auto &[Old, New] : Pinned)
    MFI.RemoveStackObject(Old);
}

void AlphaFrameLowering::emitEpilogue(MachineFunction &MF,
                                  MachineBasicBlock &MBB) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // A shrink-wrapped restore block may end in branches rather than a return:
  // the epilogue goes before the first terminator.
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  bool FP = hasFP(MF);
  const bool IsFuncletReturn =
      MBBI != MBB.end() && (MBBI->getOpcode() == Alpha::CATCHRET ||
                            MBBI->getOpcode() == Alpha::CLEANUPRET);

  // A catch funclet returns its continuation address in $0. Load it before
  // the frame is released: NT recognizes an epilogue only at the ret or at a
  // single frame release directly before it.
  if (MBBI != MBB.end() && MBBI->getOpcode() == Alpha::CATCHRET) {
    MachineBasicBlock *Target = MBBI->getOperand(0).getMBB();
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAHr), Alpha::R0)
        .addMBB(Target)
        .addReg(Alpha::R31);
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDAr), Alpha::R0)
        .addMBB(Target)
        .addReg(Alpha::R0);
  }

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
        .addImm(getFPSaveOffset(MF))
        .addReg(Alpha::R30);
  } else if (FP) {
    // copy the FP into the SP (discards allocas and realignment)
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::BISr), Alpha::R30).addReg(Alpha::R15)
      .addReg(Alpha::R15);
    //restore the FP
    BuildMI(MBB, MBBI, dl, TII.get(Alpha::LDQ), Alpha::R15)
        .addImm(getFPSaveOffset(MF))
        .addReg(Alpha::R15);
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
