//===- MipsSEFrameLowering.cpp - Mips32/64 Frame Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips32/64 implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "MipsSEFrameLowering.h"
#include "MCTargetDesc/MipsABIInfo.h"
#include "MipsMachineFunction.h"
#include "MipsRegisterInfo.h"
#include "MipsSEInstrInfo.h"
#include "MipsSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/CFIInstBuilder.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <utility>
#include <vector>

using namespace llvm;

static std::pair<unsigned, unsigned> getMFHiLoOpc(unsigned Src) {
  if (Mips::ACC64RegClass.contains(Src))
    return std::make_pair((unsigned)Mips::PseudoMFHI,
                          (unsigned)Mips::PseudoMFLO);

  if (Mips::ACC64DSPRegClass.contains(Src))
    return std::make_pair((unsigned)Mips::MFHI_DSP, (unsigned)Mips::MFLO_DSP);

  if (Mips::ACC128RegClass.contains(Src))
    return std::make_pair((unsigned)Mips::PseudoMFHI64,
                          (unsigned)Mips::PseudoMFLO64);

  return std::make_pair(0, 0);
}

namespace {

/// Helper class to expand pseudos.
class ExpandPseudo {
public:
  ExpandPseudo(MachineFunction &MF);
  bool expand();

private:
  using Iter = MachineBasicBlock::iterator;

  bool expandInstr(MachineBasicBlock &MBB, Iter I);
  void expandLoadCCond(MachineBasicBlock &MBB, Iter I);
  void expandStoreCCond(MachineBasicBlock &MBB, Iter I);
  void expandLoadACC(MachineBasicBlock &MBB, Iter I, unsigned RegSize);
  void expandStoreACC(MachineBasicBlock &MBB, Iter I, unsigned MFHiOpc,
                      unsigned MFLoOpc, unsigned RegSize);
  bool expandCopy(MachineBasicBlock &MBB, Iter I);
  bool expandCopyACC(MachineBasicBlock &MBB, Iter I, unsigned MFHiOpc,
                     unsigned MFLoOpc);
  bool expandBuildPairF64(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator I, bool FP64) const;
  bool expandExtractElementF64(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I, bool FP64) const;

  MachineFunction &MF;
  MachineRegisterInfo &MRI;
  const MipsSubtarget &Subtarget;
  const MipsSEInstrInfo &TII;
  const MipsRegisterInfo &RegInfo;
};

} // end anonymous namespace

ExpandPseudo::ExpandPseudo(MachineFunction &MF_)
    : MF(MF_), MRI(MF.getRegInfo()),
      Subtarget(MF.getSubtarget<MipsSubtarget>()),
      TII(*static_cast<const MipsSEInstrInfo *>(Subtarget.getInstrInfo())),
      RegInfo(*Subtarget.getRegisterInfo()) {}

bool ExpandPseudo::expand() {
  bool Expanded = false;

  for (auto &MBB : MF) {
    for (Iter I = MBB.begin(), End = MBB.end(); I != End;)
      Expanded |= expandInstr(MBB, I++);
  }

  return Expanded;
}

bool ExpandPseudo::expandInstr(MachineBasicBlock &MBB, Iter I) {
  switch(I->getOpcode()) {
  case Mips::LOAD_CCOND_DSP:
    expandLoadCCond(MBB, I);
    break;
  case Mips::STORE_CCOND_DSP:
    expandStoreCCond(MBB, I);
    break;
  case Mips::LOAD_ACC64:
  case Mips::LOAD_ACC64DSP:
    expandLoadACC(MBB, I, 4);
    break;
  case Mips::LOAD_ACC128:
    expandLoadACC(MBB, I, 8);
    break;
  case Mips::STORE_ACC64:
    expandStoreACC(MBB, I, Mips::PseudoMFHI, Mips::PseudoMFLO, 4);
    break;
  case Mips::STORE_ACC64DSP:
    expandStoreACC(MBB, I, Mips::MFHI_DSP, Mips::MFLO_DSP, 4);
    break;
  case Mips::STORE_ACC128:
    expandStoreACC(MBB, I, Mips::PseudoMFHI64, Mips::PseudoMFLO64, 8);
    break;
  case Mips::BuildPairF64:
    if (expandBuildPairF64(MBB, I, false))
      MBB.erase(I);
    return false;
  case Mips::BuildPairF64_64:
    if (expandBuildPairF64(MBB, I, true))
      MBB.erase(I);
    return false;
  case Mips::ExtractElementF64:
    if (expandExtractElementF64(MBB, I, false))
      MBB.erase(I);
    return false;
  case Mips::ExtractElementF64_64:
    if (expandExtractElementF64(MBB, I, true))
      MBB.erase(I);
    return false;
  case TargetOpcode::COPY:
    if (!expandCopy(MBB, I))
      return false;
    break;
  default:
    return false;
  }

  MBB.erase(I);
  return true;
}

void ExpandPseudo::expandLoadCCond(MachineBasicBlock &MBB, Iter I) {
  //  load $vr, FI
  //  copy ccond, $vr

  assert(I->getOperand(0).isReg() && I->getOperand(1).isFI());

  const TargetRegisterClass *RC = RegInfo.intRegClass(4);
  Register VR = MRI.createVirtualRegister(RC);
  Register Dst = I->getOperand(0).getReg(), FI = I->getOperand(1).getIndex();

  TII.loadRegFromStack(MBB, I, VR, FI, RC, 0);
  BuildMI(MBB, I, I->getDebugLoc(), TII.get(TargetOpcode::COPY), Dst)
    .addReg(VR, RegState::Kill);
}

void ExpandPseudo::expandStoreCCond(MachineBasicBlock &MBB, Iter I) {
  //  copy $vr, ccond
  //  store $vr, FI

  assert(I->getOperand(0).isReg() && I->getOperand(1).isFI());

  const TargetRegisterClass *RC = RegInfo.intRegClass(4);
  Register VR = MRI.createVirtualRegister(RC);
  Register Src = I->getOperand(0).getReg(), FI = I->getOperand(1).getIndex();

  BuildMI(MBB, I, I->getDebugLoc(), TII.get(TargetOpcode::COPY), VR)
    .addReg(Src, getKillRegState(I->getOperand(0).isKill()));
  TII.storeRegToStack(MBB, I, VR, true, FI, RC, 0);
}

void ExpandPseudo::expandLoadACC(MachineBasicBlock &MBB, Iter I,
                                 unsigned RegSize) {
  //  load $vr0, FI
  //  copy lo, $vr0
  //  load $vr1, FI + 4
  //  copy hi, $vr1

  assert(I->getOperand(0).isReg() && I->getOperand(1).isFI());

  const TargetRegisterClass *RC = RegInfo.intRegClass(RegSize);
  Register VR0 = MRI.createVirtualRegister(RC);
  Register VR1 = MRI.createVirtualRegister(RC);
  Register Dst = I->getOperand(0).getReg(), FI = I->getOperand(1).getIndex();
  Register Lo = RegInfo.getSubReg(Dst, Mips::sub_lo);
  Register Hi = RegInfo.getSubReg(Dst, Mips::sub_hi);
  DebugLoc DL = I->getDebugLoc();
  const MCInstrDesc &Desc = TII.get(TargetOpcode::COPY);

  TII.loadRegFromStack(MBB, I, VR0, FI, RC, 0);
  BuildMI(MBB, I, DL, Desc, Lo).addReg(VR0, RegState::Kill);
  TII.loadRegFromStack(MBB, I, VR1, FI, RC, RegSize);
  BuildMI(MBB, I, DL, Desc, Hi).addReg(VR1, RegState::Kill);
}

void ExpandPseudo::expandStoreACC(MachineBasicBlock &MBB, Iter I,
                                  unsigned MFHiOpc, unsigned MFLoOpc,
                                  unsigned RegSize) {
  //  mflo $vr0, src
  //  store $vr0, FI
  //  mfhi $vr1, src
  //  store $vr1, FI + 4

  assert(I->getOperand(0).isReg() && I->getOperand(1).isFI());

  const TargetRegisterClass *RC = RegInfo.intRegClass(RegSize);
  Register VR0 = MRI.createVirtualRegister(RC);
  Register VR1 = MRI.createVirtualRegister(RC);
  Register Src = I->getOperand(0).getReg(), FI = I->getOperand(1).getIndex();
  RegState SrcKill = getKillRegState(I->getOperand(0).isKill());
  DebugLoc DL = I->getDebugLoc();

  BuildMI(MBB, I, DL, TII.get(MFLoOpc), VR0).addReg(Src);
  TII.storeRegToStack(MBB, I, VR0, true, FI, RC, 0);
  BuildMI(MBB, I, DL, TII.get(MFHiOpc), VR1).addReg(Src, SrcKill);
  TII.storeRegToStack(MBB, I, VR1, true, FI, RC, RegSize);
}

bool ExpandPseudo::expandCopy(MachineBasicBlock &MBB, Iter I) {
  Register Src = I->getOperand(1).getReg();
  std::pair<unsigned, unsigned> Opcodes = getMFHiLoOpc(Src);

  if (!Opcodes.first)
    return false;

  return expandCopyACC(MBB, I, Opcodes.first, Opcodes.second);
}

bool ExpandPseudo::expandCopyACC(MachineBasicBlock &MBB, Iter I,
                                 unsigned MFHiOpc, unsigned MFLoOpc) {
  //  mflo $vr0, src
  //  copy dst_lo, $vr0
  //  mfhi $vr1, src
  //  copy dst_hi, $vr1

  unsigned Dst = I->getOperand(0).getReg(), Src = I->getOperand(1).getReg();
  const TargetRegisterClass *DstRC = RegInfo.getMinimalPhysRegClass(Dst);
  unsigned VRegSize = RegInfo.getRegSizeInBits(*DstRC) / 16;
  const TargetRegisterClass *RC = RegInfo.intRegClass(VRegSize);
  Register VR0 = MRI.createVirtualRegister(RC);
  Register VR1 = MRI.createVirtualRegister(RC);
  RegState SrcKill = getKillRegState(I->getOperand(1).isKill());
  Register DstLo = RegInfo.getSubReg(Dst, Mips::sub_lo);
  Register DstHi = RegInfo.getSubReg(Dst, Mips::sub_hi);
  DebugLoc DL = I->getDebugLoc();

  BuildMI(MBB, I, DL, TII.get(MFLoOpc), VR0).addReg(Src);
  BuildMI(MBB, I, DL, TII.get(TargetOpcode::COPY), DstLo)
    .addReg(VR0, RegState::Kill);
  BuildMI(MBB, I, DL, TII.get(MFHiOpc), VR1).addReg(Src, SrcKill);
  BuildMI(MBB, I, DL, TII.get(TargetOpcode::COPY), DstHi)
    .addReg(VR1, RegState::Kill);
  return true;
}

/// This method expands the same instruction that MipsSEInstrInfo::
/// expandBuildPairF64 does, for FPXX/FP64 when mthc1 is not available and
/// for FP64A. It is implemented here
/// because frame indexes are eliminated before MipsSEInstrInfo::
/// expandBuildPairF64 is called.
bool ExpandPseudo::expandBuildPairF64(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator I,
                                      bool FP64) const {
  // For FPXX/FP64 when mthc1 is not available, use:
  //   spill + reload via ldc1
  //
  // The case where 64-bit GPRs can be used doesn't need to be handled here
  // because it never creates a BuildPairF64 node.
  //
  // The FP64A ABI (fp64 with nooddspreg) must also use a spill/reload sequence
  // for odd-numbered double precision values (because the lower 32-bits is
  // transferred with mtc1 which is redirected to the upper half of the even
  // register). Unfortunately, we have to make this decision before register
  // allocation so for now we use a spill/reload sequence for all
  // double-precision values in regardless of being an odd/even register.
  //
  // For the cases that should be covered here MipsSEISelDAGToDAG adds $sp as
  // implicit operand, so other passes (like ShrinkWrapping) are aware that
  // stack is used.
  if (I->getNumOperands() == 4 && I->getOperand(3).isReg()
      && I->getOperand(3).getReg() == Mips::SP) {
    Register DstReg = I->getOperand(0).getReg();
    Register LoReg = I->getOperand(1).getReg();
    Register HiReg = I->getOperand(2).getReg();

    const TargetRegisterClass *RC = &Mips::GPR32RegClass;
    const TargetRegisterClass *RC2 =
        FP64 ? &Mips::FGR64RegClass : &Mips::AFGR64RegClass;

    // We re-use the same spill slot each time so that the stack frame doesn't
    // grow too much in functions with a large number of moves.
    int FI = MF.getInfo<MipsFunctionInfo>()->getMoveF64ViaSpillFI(MF, RC2);
    if (!Subtarget.isLittle())
      std::swap(LoReg, HiReg);
    TII.storeRegToStack(MBB, I, LoReg, I->getOperand(1).isKill(), FI, RC, 0);
    TII.storeRegToStack(MBB, I, HiReg, I->getOperand(2).isKill(), FI, RC, 4);
    TII.loadRegFromStack(MBB, I, DstReg, FI, RC2, 0);
    return true;
  }

  return false;
}

/// This method expands the same instruction that MipsSEInstrInfo::
/// expandExtractElementF64 does, for FPXX/FP64 when mfhc1 is not available and
/// for FP64A. It is implemented here
/// because frame indexes are eliminated before MipsSEInstrInfo::
/// expandExtractElementF64 is called.
bool ExpandPseudo::expandExtractElementF64(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator I,
                                           bool FP64) const {
  const MachineOperand &Op1 = I->getOperand(1);
  const MachineOperand &Op2 = I->getOperand(2);

  if ((Op1.isReg() && Op1.isUndef()) || (Op2.isReg() && Op2.isUndef())) {
    Register DstReg = I->getOperand(0).getReg();
    BuildMI(MBB, I, I->getDebugLoc(), TII.get(Mips::IMPLICIT_DEF), DstReg);
    return true;
  }

  // For FPXX/FP64 when mfhc1 is not available, use:
  //   spill + reload via ldc1
  //
  // The case where 64-bit GPRs can be used doesn't need to be handled here
  // because it never creates a ExtractElementF64 node.
  //
  // The FP64A ABI (fp64 with nooddspreg) must also use a spill/reload sequence
  // for odd-numbered double precision values (because the lower 32-bits is
  // transferred with mfc1 which is redirected to the upper half of the even
  // register). Unfortunately, we have to make this decision before register
  // allocation so for now we use a spill/reload sequence for all
  // double-precision values in regardless of being an odd/even register.
  //
  // For the cases that should be covered here MipsSEISelDAGToDAG adds $sp as
  // implicit operand, so other passes (like ShrinkWrapping) are aware that
  // stack is used.
  if (I->getNumOperands() == 4 && I->getOperand(3).isReg()
      && I->getOperand(3).getReg() == Mips::SP) {
    Register DstReg = I->getOperand(0).getReg();
    Register SrcReg = Op1.getReg();
    unsigned N = Op2.getImm();
    int64_t Offset = 4 * (Subtarget.isLittle() ? N : (1 - N));

    const TargetRegisterClass *RC =
        FP64 ? &Mips::FGR64RegClass : &Mips::AFGR64RegClass;
    const TargetRegisterClass *RC2 = &Mips::GPR32RegClass;

    // We re-use the same spill slot each time so that the stack frame doesn't
    // grow too much in functions with a large number of moves.
    int FI = MF.getInfo<MipsFunctionInfo>()->getMoveF64ViaSpillFI(MF, RC);
    TII.storeRegToStack(MBB, I, SrcReg, Op1.isKill(), FI, RC, 0);
    TII.loadRegFromStack(MBB, I, DstReg, FI, RC2, Offset);
    return true;
  }

  return false;
}

MipsSEFrameLowering::MipsSEFrameLowering(const MipsSubtarget &STI)
    : MipsFrameLowering(STI, STI.getStackAlignment()) {}

static unsigned getFuncletFrameNestLevel(const MachineBasicBlock &MBB) {
  const BasicBlock *BB = MBB.getBasicBlock();
  if (!MBB.isEHFuncletEntry() || !BB)
    return 1;

  const auto *Pad = dyn_cast<FuncletPadInst>(BB->getFirstNonPHIIt());
  if (!Pad)
    return 1;

  unsigned Level = 2;
  const Value *ParentPad = nullptr;
  if (const auto *CatchPad = dyn_cast<CatchPadInst>(Pad))
    ParentPad = CatchPad->getCatchSwitch()->getParentPad();
  else
    ParentPad = cast<CleanupPadInst>(Pad)->getParentPad();

  while (const auto *Parent = dyn_cast<FuncletPadInst>(ParentPad)) {
    ++Level;
    if (const auto *CatchPad = dyn_cast<CatchPadInst>(Parent))
      ParentPad = CatchPad->getCatchSwitch()->getParentPad();
    else
      ParentPad = cast<CleanupPadInst>(Parent)->getParentPad();
  }
  return Level;
}

// The NT4 MIPS __CxxFrameHandler keeps one "already unwound" byte per state
// at dispUnwindHelp from the outermost parent's
// establisher frame: an action runs only if its byte is zero, and the
// runtime sets the byte first. A catch funclet's
// FuncInfo can reach the parent's states through the same map, so these
// bytes keep each action from running twice. Allocate the bytes, clear them
// on entry, and clear the states of a try block again where its catches
// return, so that a state entered again after a catch can be unwound again.
static void createLegacyCXXUnwindHelp(MachineFunction &MF,
                                      WinEHFuncInfo &EHInfo,
                                      const TargetInstrInfo &TII) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const unsigned NumStates = EHInfo.CxxUnwindMap.size();
  const unsigned Size = alignTo(NumStates, 4);
  const int FI = MFI.CreateStackObject(Size, Align(4), /*isSpillSlot=*/false);
  EHInfo.UnwindHelpFrameIdx = FI;

  auto Clear = [&](MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   unsigned Lo, unsigned Hi) {
    for (unsigned B = Lo; B <= Hi;) {
      bool Word = B % 4 == 0 && B + 3 <= Hi;
      BuildMI(MBB, I, DebugLoc(), TII.get(Word ? Mips::SW : Mips::SB))
          .addReg(Mips::R0)
          .addFrameIndex(FI)
          .addImm(B);
      B += Word ? 4 : 1;
    }
  };

  // After the callee-saved register spills, which emitPrologue expects at the
  // start of the entry block.
  MachineBasicBlock &Entry = MF.front();
  MachineBasicBlock::iterator EntryI = Entry.begin();
  while (EntryI != Entry.end() && EntryI->getFlag(MachineInstr::FrameSetup))
    ++EntryI;
  Clear(Entry, EntryI, 0, Size - 1);

  DenseMap<MachineBasicBlock *, std::pair<int, int>> Continuations;
  for (MachineBasicBlock &MBB : MF) {
    MachineBasicBlock::iterator Term = MBB.getFirstTerminator();
    if (Term == MBB.end() || Term->getOpcode() != Mips::CATCHRET)
      continue;
    auto Scope = EHInfo.EHScopeMembership.find(&MBB);
    if (Scope == EHInfo.EHScopeMembership.end())
      continue;
    const MachineBasicBlock *Funclet = MF.getBlockNumbered(Scope->second);
    for (const WinEHTryBlockMapEntry &TBME : EHInfo.TryBlockMap) {
      if (llvm::none_of(TBME.HandlerArray, [&](const WinEHHandlerType &H) {
            return dyn_cast_if_present<MachineBasicBlock *>(H.Handler) ==
                   Funclet;
          }))
        continue;
      MachineBasicBlock *Dst = Term->getOperand(0).getMBB();
      auto [It, New] = Continuations.try_emplace(
          Dst, std::make_pair(TBME.TryLow, TBME.CatchHigh));
      if (!New) {
        It->second.first = std::min(It->second.first, TBME.TryLow);
        It->second.second = std::max(It->second.second, TBME.CatchHigh);
      }
    }
  }
  for (auto &[Dst, Range] : Continuations)
    Clear(*Dst, Dst->begin(), Range.first, Range.second);
}

void MipsSEFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *) const {
  if (!STI.getTargetTriple().isOSWindows())
    return;

  // SEH unwind opcodes are only emitted under the WinEH exception model.
  if (MF.needsWinCFI())
    MF.setHasWinCFI(true);

  if (!MF.hasEHFunclets())
    return;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  WinEHFuncInfo &EHInfo = *MF.getWinEHFuncInfo();
  EHInfo.EHScopeMembership = getEHScopeMembership(MF);

  // Funclets share the parent's frame size but never move $sp in their bodies
  // (see eliminateCallFramePseudoInstr), so when the parent has no reserved
  // call frame, its frame must still be large enough for a funclet's
  // outgoing arguments at the bottom of the funclet's copy.
  if (!hasReservedCallFrame(MF) && MFI.getMaxCallFrameSize())
    MFI.CreateStackObject(alignTo(MFI.getMaxCallFrameSize(), getStackAlign()),
                          getStackAlign(), /*isSpillSlot=*/false);

  const Function &F = MF.getFunction();
  const auto *PerFn =
      F.hasPersonalityFn()
          ? dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts())
          : nullptr;
  if (PerFn && PerFn->getName() == "__CxxFrameHandler" &&
      !EHInfo.CxxUnwindMap.empty())
    createLegacyCXXUnwindHelp(MF, EHInfo, *STI.getInstrInfo());

  // The legacy runtime follows a funclet's static link at its establisher
  // frame - 4, which is the incoming SP - 4 unless the frame is allocated in
  // two steps.
  int64_t CurrentOffset = -4;
  MFI.CreateFixedObject(/*Size=*/4, CurrentOffset, /*IsImmutable=*/false);
  for (WinEHTryBlockMapEntry &TBME : EHInfo.TryBlockMap) {
    for (WinEHHandlerType &Handler : TBME.HandlerArray) {
      int FrameIndex = Handler.CatchObj.FrameIndex;
      if (FrameIndex == INT_MAX || MFI.getObjectOffset(FrameIndex) != 0)
        continue;
      CurrentOffset -= MFI.getObjectSize(FrameIndex);
      CurrentOffset =
          alignDown(CurrentOffset, MFI.getObjectAlign(FrameIndex).value());
      MFI.setObjectOffset(FrameIndex, CurrentOffset);
    }
  }

  // NTUW's __CxxFrameHandler3 keeps the try-block state of a frame being
  // unwound in a word at dispUnwindHelp from the establisher frame, which
  // starts as -2.
  if (PerFn && PerFn->getName() == "__CxxFrameHandler3" &&
      STI.getTargetTriple().usesNTRISCTableUnwind()) {
    for (int I = MFI.getObjectIndexBegin(); I < 0; ++I)
      CurrentOffset = std::min(CurrentOffset, MFI.getObjectOffset(I));
    CurrentOffset -= 4;
    int FI = MFI.CreateFixedObject(4, CurrentOffset, /*IsImmutable=*/false);
    EHInfo.UnwindHelpFrameIdx = FI;
    MachineBasicBlock &Entry = MF.front();
    MachineBasicBlock::iterator EntryI = Entry.begin();
    while (EntryI != Entry.end() && EntryI->getFlag(MachineInstr::FrameSetup))
      ++EntryI;
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    BuildMI(Entry, EntryI, DebugLoc(), TII.get(Mips::ADDiu), Mips::R1)
        .addReg(Mips::R0)
        .addImm(-2);
    BuildMI(Entry, EntryI, DebugLoc(), TII.get(Mips::SW))
        .addReg(Mips::R1, RegState::Kill)
        .addFrameIndex(FI)
        .addImm(0);
  }
}

void MipsSEFrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI    = MF.getFrameInfo();
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();

  const MipsSEInstrInfo &TII =
      *static_cast<const MipsSEInstrInfo *>(STI.getInstrInfo());
  const MipsRegisterInfo &RegInfo = *STI.getRegisterInfo();

  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc dl;
  MipsABIInfo ABI = STI.getABI();
  unsigned SP = ABI.GetStackPtr();
  unsigned FP = ABI.GetFramePtr();
  unsigned ZERO = ABI.GetNullPtr();
  unsigned MOVE = ABI.GetGPRMoveOp();
  unsigned ADDiu = ABI.GetPtrAddiuOp();
  unsigned AND = ABI.IsN64() ? Mips::AND64 : Mips::AND;

  const TargetRegisterClass *RC = ABI.ArePtrs64bit() ?
        &Mips::GPR64RegClass : &Mips::GPR32RegClass;

  // First, compute final stack size.
  uint64_t StackSize = MFI.getStackSize();

  uint64_t WinSaveArea = 0;
  auto EmitPrologEnd = [&]() {
    if (!STI.getTargetTriple().isOSWindows() || !MF.usesWindowsUnwindInfo() ||
        !(MF.hasWinCFI() || MF.hasEHFunclets() ||
          MF.getFunction().needsUnwindTableEntry()))
      return;
    // NTUW states the establisher frame that NT 4.0 derives from a two-step
    // allocation, so that both formats share one frame layout.
    if (WinSaveArea && STI.getTargetTriple().usesNTRISCTableUnwind())
      BuildMI(MBB, MBBI, dl, TII.get(Mips::SEH_Establisher))
          .addImm(WinSaveArea)
          .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MBBI, dl, TII.get(Mips::SEH_PrologEnd))
        .setMIFlag(MachineInstr::FrameSetup);
  };

  // SEH filters reach the parent's llvm.localaddress (its incoming $sp) by
  // adding this offset to the establisher frame (see eh_recoverfp lowering).
  const bool IsSEHParent =
      STI.getTargetTriple().isOSWindows() && !MBB.isEHFuncletEntry() &&
      MF.hasEHFunclets() &&
      classifyEHPersonality(MF.getFunction().getPersonalityFn()) ==
          EHPersonality::MSVC_TableSEH;
  if (IsSEHParent)
    MF.getWinEHFuncInfo()->SEHSetFrameOffset = 0;

  // No need to allocate space on the stack.
  if (StackSize == 0 && !MFI.adjustsStack()) {
    EmitPrologEnd();
    return;
  }

  CFIInstBuilder CFIBuilder(MBB, MBBI, MachineInstr::NoFlags);

  // The NT unwinder reverse-executes the prologue. It recognizes a register
  // save only as `sw rt, imm16($sp)` and a large allocation only as
  // `ori t, $zero, n` or `lui t, hi; ori t, t, lo` followed by
  // `subu $sp, $sp, t`. A frame too large for one addiu is therefore allocated
  // in two steps, with the callee-saved area first so that its stores reach
  // every slot from the intermediate $sp.
  const bool IsNT = STI.getTargetTriple().isOSWindows() && !ABI.IsN64();
  if (IsNT && !isInt<16>(StackSize)) {
    for (const CalleeSavedInfo &I : MFI.getCalleeSavedInfo())
      WinSaveArea = std::max<uint64_t>(WinSaveArea,
                                       -MFI.getObjectOffset(I.getFrameIdx()));
    if (MF.hasEHFunclets())
      WinSaveArea = std::max<uint64_t>(WinSaveArea, 4);
    WinSaveArea = alignTo(WinSaveArea, getStackAlign());
  }
  MipsFI->setWinPrologueSaveAreaSize(WinSaveArea);
  if (IsSEHParent)
    MF.getWinEHFuncInfo()->SEHSetFrameOffset = static_cast<int>(WinSaveArea);

  auto EmitNTStackDecrement = [&](uint64_t Amount) {
    assert(Amount <= UINT32_MAX && "Windows MIPS frame too large");
    Register Tmp = MF.getRegInfo().createVirtualRegister(&Mips::GPR32RegClass);
    if (Amount <= 0xffff) {
      BuildMI(MBB, MBBI, dl, TII.get(Mips::ORi), Tmp)
          .addReg(ZERO)
          .addImm(Amount)
          .setMIFlag(MachineInstr::FrameSetup);
    } else {
      // NT reads the low half only from the ori, so emit it even when zero.
      BuildMI(MBB, MBBI, dl, TII.get(Mips::LUi), Tmp)
          .addImm(Amount >> 16)
          .setMIFlag(MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, dl, TII.get(Mips::ORi), Tmp)
          .addReg(Tmp, RegState::Kill)
          .addImm(Amount & 0xffff)
          .setMIFlag(MachineInstr::FrameSetup);
    }
    BuildMI(MBB, MBBI, dl, TII.get(Mips::SUBu), SP)
        .addReg(SP)
        .addReg(Tmp, RegState::Kill)
        .setMIFlag(MachineInstr::FrameSetup);
  };

  // NT commits a stack only through the guard page below it, so a frame
  // larger than a probe interval touches each of its pages before $sp
  // moves. inlineStackProbe expands this marker, placed
  // directly before the allocation step that crosses the pages.
  const MipsTargetLowering &TLI = *STI.getTargetLowering();
  const bool Probe = IsNT && TLI.hasInlineStackProbe(MF) &&
                     StackSize > TLI.getStackProbeSize(MF);
  auto EmitProbe = [&](uint64_t Current) {
    BuildMI(MBB, MBBI, dl, TII.get(Mips::NT_PROBE_STACK))
        .addImm(Current)
        .addImm(StackSize)
        .setMIFlag(MachineInstr::FrameSetup);
  };

  // Adjust stack. A two-step allocation probes before its second step.
  if (!WinSaveArea && Probe)
    EmitProbe(0);
  if (WinSaveArea)
    TII.adjustStackPtr(SP, -(int64_t)WinSaveArea, MBB, MBBI);
  else if (IsNT && !isInt<16>(StackSize))
    EmitNTStackDecrement(StackSize);
  else
    TII.adjustStackPtr(SP, -StackSize, MBB, MBBI);
  CFIBuilder.buildDefCFAOffset(StackSize);

  if (MF.getFunction().hasFnAttribute("interrupt"))
    emitInterruptPrologueStub(MF, MBB);

  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // Find the instruction past the last instruction that saves a callee-saved
  // register to the stack.
  std::advance(MBBI, CSI.size());
  CFIBuilder.setInsertPoint(MBBI);

  if (!CSI.empty()) {
    // Iterate over list of callee-saved registers and emit .cfi_offset
    // directives.
    for (const CalleeSavedInfo &I : CSI) {
      int64_t Offset = MFI.getObjectOffset(I.getFrameIdx());
      MCRegister Reg = I.getReg();

      // If Reg is a double precision register, emit two cfa_offsets,
      // one for each of the paired single precision registers.
      if (Mips::AFGR64RegClass.contains(Reg)) {
        MCRegister Reg0 = RegInfo.getSubReg(Reg, Mips::sub_lo);
        MCRegister Reg1 = RegInfo.getSubReg(Reg, Mips::sub_hi);

        if (!STI.isLittle())
          std::swap(Reg0, Reg1);

        CFIBuilder.buildOffset(Reg0, Offset);
        CFIBuilder.buildOffset(Reg1, Offset + 4);
      } else if (Mips::FGR64RegClass.contains(Reg)) {
        MCRegister Reg0 = Reg;
        MCRegister Reg1 = Reg + 1;

        if (!STI.isLittle())
          std::swap(Reg0, Reg1);

        CFIBuilder.buildOffset(Reg0, Offset);
        CFIBuilder.buildOffset(Reg1, Offset + 4);
      } else {
        // Reg is either in GPR32 or FGR32.
        CFIBuilder.buildOffset(Reg, Offset);
      }
    }
  }

  if (MipsFI->callsEhReturn()) {
    // Insert instructions that spill eh data registers.
    for (int I = 0; I < 4; ++I) {
      if (!MBB.isLiveIn(ABI.GetEhDataReg(I)))
        MBB.addLiveIn(ABI.GetEhDataReg(I));
      TII.storeRegToStackSlot(MBB, MBBI, ABI.GetEhDataReg(I), false,
                              MipsFI->getEhDataRegFI(I), RC, Register());
    }

    // Emit .cfi_offset directives for eh data registers.
    for (int I = 0; I < 4; ++I) {
      int64_t Offset = MFI.getObjectOffset(MipsFI->getEhDataRegFI(I));
      CFIBuilder.buildOffset(ABI.GetEhDataReg(I), Offset);
    }
  }

  // A funclet stores the frame it was called with (its static link, in $v0)
  // at its own establisher frame - 4, where the runtime and nested funclets
  // (below) look for it. The establisher frame is the
  // incoming $sp less any first allocation step, so a two-step frame stores
  // it once the whole frame is allocated.
  const bool IsFunclet = MBB.isEHFuncletEntry();
  const bool StoresStaticLink =
      IsFunclet && STI.getTargetTriple().isOSWindows();
  // NT 4.0 passes a funclet its frame in $v0; NTUW passes the establisher
  // frame as the second argument.
  const bool IsNTUW = STI.getTargetTriple().usesNTRISCTableUnwind();
  const unsigned StaticLink = IsNTUW ? Mips::R5 : Mips::R2;
  if (StoresStaticLink) {
    MBB.addLiveIn(StaticLink);
    if (!WinSaveArea)
      BuildMI(MBB, MBBI, dl, TII.get(Mips::SW))
          .addReg(StaticLink)
          .addReg(SP)
          .addImm(StackSize - 4)
          .setMIFlag(MachineInstr::FrameSetup);
  }

  if (WinSaveArea) {
    if (Probe)
      EmitProbe(WinSaveArea);
    EmitNTStackDecrement(StackSize - WinSaveArea);

    if (StoresStaticLink) {
      // The funclet's copy of the frame below the callee-saved area is free
      // above its outgoing arguments.
      int64_t Offset = StackSize - WinSaveArea - 4;
      assert(Offset >= (int64_t)MFI.getMaxCallFrameSize() &&
             "Static link overlaps the funclet's outgoing arguments");
      Register Base = SP;
      if (!isInt<16>(Offset)) {
        unsigned NewImm = 0;
        Register Reg = TII.loadImmediate(Offset, MBB, MBBI, dl, &NewImm);
        BuildMI(MBB, MBBI, dl, TII.get(Mips::ADDu), Reg)
            .addReg(SP)
            .addReg(Reg, RegState::Kill)
            .setMIFlag(MachineInstr::FrameSetup);
        Base = Reg;
        Offset = SignExtend64<16>(NewImm);
      }
      BuildMI(MBB, MBBI, dl, TII.get(Mips::SW))
          .addReg(StaticLink)
          .addReg(Base, getKillRegState(Base != SP))
          .addImm(Offset)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  // If framepointer enabled, set it to the local frame for a parent function
  // or to the outermost parent frame passed in $v0 for a funclet.
  if (hasFP(MF)) {
    unsigned FrameSource = IsFunclet ? StaticLink : SP;
    BuildMI(MBB, MBBI, dl, TII.get(MOVE), FP)
        .addReg(FrameSource)
        .addReg(ZERO)
        .setMIFlag(MachineInstr::FrameSetup);

    // The NTUW runtime passes every funclet the outermost parent's frame.
    if (IsFunclet && !IsNTUW) {
      for (unsigned Level = getFuncletFrameNestLevel(MBB); Level > 2; --Level)
        BuildMI(MBB, MBBI, dl, TII.get(Mips::LW), FP)
            .addReg(FP)
            .addImm(-4)
            .setMIFlag(MachineInstr::FrameSetup);
    } else if (!IsFunclet) {
      CFIBuilder.buildDefCFARegister(FP);
    }

    if (RegInfo.hasStackRealignment(MF)) {
      // addiu $Reg, $zero, -MaxAlignment
      // andi $sp, $sp, $Reg
      // An NTUW funclet keeps its own frame unaligned: it holds only spills,
      // and NTUW must find the funclet's CFA in a register.
      if (!(IsFunclet && IsNTUW)) {
        Register VR = MF.getRegInfo().createVirtualRegister(RC);
        assert((Log2(MFI.getMaxAlign()) < 16) &&
               "Function's alignment size requirement is not supported.");
        int64_t MaxAlign = -(int64_t)MFI.getMaxAlign().value();

        BuildMI(MBB, MBBI, dl, TII.get(ADDiu), VR)
            .addReg(ZERO)
            .addImm(MaxAlign);
        BuildMI(MBB, MBBI, dl, TII.get(AND), SP).addReg(SP).addReg(VR);
      }

      if (hasBP(MF)) {
        // move $s7, $sp
        unsigned BP = STI.getABI().GetBasePtr();
        BuildMI(MBB, MBBI, dl, TII.get(MOVE), BP)
          .addReg(SP)
          .addReg(ZERO);
      }
    }
  }

  EmitPrologEnd();
}

void MipsSEFrameLowering::emitInterruptPrologueStub(
    MachineFunction &MF, MachineBasicBlock &MBB) const {
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();
  MachineBasicBlock::iterator MBBI = MBB.begin();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Report an error the target doesn't support Mips32r2 or later.
  // The epilogue relies on the use of the "ehb" to clear execution
  // hazards. Pre R2 Mips relies on an implementation defined number
  // of "ssnop"s to clear the execution hazard. Support for ssnop hazard
  // clearing is not provided so reject that configuration.
  if (!STI.hasMips32r2())
    report_fatal_error(
        "\"interrupt\" attribute is not supported on pre-MIPS32R2 or "
        "MIPS16 targets.");

  // The GP register contains the "user" value, so we cannot perform
  // any gp relative loads until we restore the "kernel" or "system" gp
  // value. Until support is written we shall only accept the static
  // relocation model.
  if ((STI.getRelocationModel() != Reloc::Static))
    report_fatal_error("\"interrupt\" attribute is only supported for the "
                       "static relocation model on MIPS at the present time.");

  if (!STI.isABI_O32() || STI.hasMips64())
    report_fatal_error("\"interrupt\" attribute is only supported for the "
                       "O32 ABI on MIPS32R2+ at the present time.");

  // Perform ISR handling like GCC
  StringRef IntKind =
      MF.getFunction().getFnAttribute("interrupt").getValueAsString();
  const TargetRegisterClass *PtrRC = &Mips::GPR32RegClass;

  // EIC interrupt handling needs to read the Cause register to disable
  // interrupts.
  if (IntKind == "eic") {
    // Coprocessor registers are always live per se.
    MBB.addLiveIn(Mips::COP013);
    BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::MFC0), Mips::R26)
        .addReg(Mips::COP013)
        .setMIFlag(MachineInstr::FrameSetup);

    BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::EXT), Mips::R26)
        .addReg(Mips::R26)
        .addImm(10)
        .addImm(6)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Fetch and spill EPC
  MBB.addLiveIn(Mips::COP014);
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::MFC0), Mips::R27)
      .addReg(Mips::COP014)
      .setMIFlag(MachineInstr::FrameSetup);

  STI.getInstrInfo()->storeRegToStack(MBB, MBBI, Mips::R27, false,
                                      MipsFI->getISRRegFI(0), PtrRC, 0);

  // Fetch and Spill Status
  MBB.addLiveIn(Mips::COP012);
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::MFC0), Mips::R27)
      .addReg(Mips::COP012)
      .setMIFlag(MachineInstr::FrameSetup);

  STI.getInstrInfo()->storeRegToStack(MBB, MBBI, Mips::R27, false,
                                      MipsFI->getISRRegFI(1), PtrRC, 0);

  // Build the configuration for disabling lower priority interrupts. Non EIC
  // interrupts need to be masked off with zero, EIC from the Cause register.
  unsigned InsPosition = 8;
  unsigned InsSize = 0;
  unsigned SrcReg = Mips::R0;

  // If the interrupt we're tied to is the EIC, switch the source for the
  // masking off interrupts to the cause register.
  if (IntKind == "eic") {
    SrcReg = Mips::R26;
    InsPosition = 10;
    InsSize = 6;
  } else
    InsSize = StringSwitch<unsigned>(IntKind)
                  .Case("sw0", 1)
                  .Case("sw1", 2)
                  .Case("hw0", 3)
                  .Case("hw1", 4)
                  .Case("hw2", 5)
                  .Case("hw3", 6)
                  .Case("hw4", 7)
                  .Case("hw5", 8)
                  .Default(0);
  assert(InsSize != 0 && "Unknown interrupt type!");

  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::INS), Mips::R27)
      .addReg(SrcReg)
      .addImm(InsPosition)
      .addImm(InsSize)
      .addReg(Mips::R27)
      .setMIFlag(MachineInstr::FrameSetup);

  // Mask off KSU, ERL, EXL
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::INS), Mips::R27)
      .addReg(Mips::R0)
      .addImm(1)
      .addImm(4)
      .addReg(Mips::R27)
      .setMIFlag(MachineInstr::FrameSetup);

  // Disable the FPU as we are not spilling those register sets.
  if (!STI.useSoftFloat())
    BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::INS), Mips::R27)
        .addReg(Mips::R0)
        .addImm(29)
        .addImm(1)
        .addReg(Mips::R27)
        .setMIFlag(MachineInstr::FrameSetup);

  // Set the new status
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::MTC0), Mips::COP012)
      .addReg(Mips::R27)
      .setMIFlag(MachineInstr::FrameSetup);
}

// Expand NT_PROBE_STACK, which sits directly before the allocation step that
// moves $sp from CFA - Cur down to CFA - Final (CFA = incoming $sp). Store
// $zero at CFA - k * PS for k = 1..N, N = (Final - 1) / PS, then at
// CFA - Final, so that no store is a page or more below the one before it and
// the page holding the new $sp is committed, as Windows' own stack check
// does.
//
// The NT unwinder reverse-executes every prologue instruction: `sw rt, n($sp)`
// reloads rt, `or/addu rd, rs, $zero` copies rd back into rs, and `addiu/ori
// rt, $zero, n` or `lui rt, n` adds to $sp when rt is the register of the `subu
// $sp, $sp, rt` that follows. The probes therefore address memory through $t8
// only, set it with `addiu $t8, $sp, n`, and count in $t7; none of these is
// reversed, and none writes $sp. The scavenged register of the allocation is
// $at.
void MipsSEFrameLowering::inlineStackProbe(MachineFunction &MF,
                                           MachineBasicBlock &MBB) const {
  auto Where = llvm::find_if(MBB, [](const MachineInstr &MI) {
    return MI.getOpcode() == Mips::NT_PROBE_STACK;
  });
  if (Where == MBB.end())
    return;
  MachineInstr &MI = *Where;
  const MipsSEInstrInfo &TII =
      *static_cast<const MipsSEInstrInfo *>(STI.getInstrInfo());
  const MipsTargetLowering &TLI = *STI.getTargetLowering();
  const unsigned SP = Mips::SP, ZERO = Mips::R0;
  const unsigned Ptr = Mips::R24, Count = Mips::R15; // $t8, $t7
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
  // The implicit $sp use keeps each store ahead of the allocation.
  auto Store = [&](MachineBasicBlock &B, MachineBasicBlock::iterator I,
                   int64_t Off) {
    Flag(BuildMI(B, I, DL, TII.get(Mips::SW))
             .addReg(ZERO)
             .addReg(Ptr)
             .addImm(Off)
             .addReg(SP, RegState::Implicit));
  };
  auto Step = [&](MachineBasicBlock &B, MachineBasicBlock::iterator I) {
    Flag(BuildMI(B, I, DL, TII.get(Mips::ADDiu), Ptr).addReg(Ptr).addImm(-PS));
  };

  // $t8 = CFA - PS.
  Flag(BuildMI(MBB, MBBI, DL, TII.get(Mips::ADDiu), Ptr)
           .addReg(SP)
           .addImm(Cur - PS));
  if (N <= 4) {
    Store(MBB, MBBI, 0);
    for (int64_t K = 2; K <= N; ++K) {
      Step(MBB, MBBI);
      Store(MBB, MBBI, 0);
    }
    // $t8 = CFA - N * PS, and N * PS < Final <= (N + 1) * PS.
    Store(MBB, MBBI, N * PS - Final);
    MI.eraseFromParent();
    return;
  }

  if (isInt<16>(N)) {
    Flag(BuildMI(MBB, MBBI, DL, TII.get(Mips::ADDiu), Count)
             .addReg(ZERO)
             .addImm(N));
  } else {
    Flag(BuildMI(MBB, MBBI, DL, TII.get(Mips::LUi), Count).addImm(N >> 16));
    Flag(BuildMI(MBB, MBBI, DL, TII.get(Mips::ORi), Count)
             .addReg(Count)
             .addImm(N & 0xffff));
  }

  // loop: sw $zero, 0($t8); addiu $t7, $t7, -1; addiu $t8, $t8, -PS;
  //       bne $t7, $zero, loop
  // The pointer step is last so that the delay slot filler moves it, and not
  // the store, under the branch.
  const BasicBlock *BB = MBB.getBasicBlock();
  MachineFunction::iterator InsertPt = std::next(MBB.getIterator());
  MachineBasicBlock *LoopMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(InsertPt, LoopMBB);
  MachineBasicBlock *ExitMBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(InsertPt, ExitMBB);
  Store(*LoopMBB, LoopMBB->end(), 0);
  Flag(BuildMI(LoopMBB, DL, TII.get(Mips::ADDiu), Count)
           .addReg(Count)
           .addImm(-1));
  Step(*LoopMBB, LoopMBB->end());
  Flag(BuildMI(LoopMBB, DL, TII.get(Mips::BNE))
           .addReg(Count)
           .addReg(ZERO)
           .addMBB(LoopMBB));
  LoopMBB->addSuccessor(LoopMBB);
  LoopMBB->addSuccessor(ExitMBB);
  ExitMBB->splice(ExitMBB->end(), &MBB, std::next(MBBI), MBB.end());
  ExitMBB->transferSuccessorsAndUpdatePHIs(&MBB);
  // Frame indices in a funclet resolve against the parent's frame, so the new
  // blocks of a funclet prologue stay in the funclet's scope.
  if (MF.hasEHFunclets() && MF.getWinEHFuncInfo()) {
    auto &Scopes = MF.getWinEHFuncInfo()->EHScopeMembership;
    auto It = Scopes.find(&MBB);
    if (It != Scopes.end()) {
      int Scope = It->second;
      Scopes[LoopMBB] = Scope;
      Scopes[ExitMBB] = Scope;
    }
  }
  MBB.addSuccessor(LoopMBB);
  // $t8 = CFA - (N + 1) * PS.
  Store(*ExitMBB, ExitMBB->begin(), (N + 1) * PS - Final);
  MI.eraseFromParent();
  fullyRecomputeLiveIns({ExitMBB, LoopMBB});
}

void MipsSEFrameLowering::emitEpilogue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  MachineFrameInfo &MFI            = MF.getFrameInfo();
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();

  const MipsSEInstrInfo &TII =
      *static_cast<const MipsSEInstrInfo *>(STI.getInstrInfo());

  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();
  MipsABIInfo ABI = STI.getABI();
  unsigned SP = ABI.GetStackPtr();
  unsigned FP = ABI.GetFramePtr();
  unsigned ZERO = ABI.GetNullPtr();
  unsigned MOVE = ABI.GetGPRMoveOp();

  // A funclet's $fp addresses its parent's frame, so a funclet return block
  // (which need not be the funclet's entry block) must not take $sp from it.
  const bool IsFuncletReturn =
      MBBI != MBB.end() && (MBBI->getOpcode() == Mips::CATCHRET ||
                            MBBI->getOpcode() == Mips::CLEANUPRET);

  // if framepointer enabled, restore the stack pointer.
  if (hasFP(MF) && !MBB.isEHFuncletEntry() && !IsFuncletReturn) {
    // Find the first instruction that restores a callee-saved register.
    MachineBasicBlock::iterator I = MBBI;

    for (unsigned i = 0; i < MFI.getCalleeSavedInfo().size(); ++i)
      --I;

    // Insert instruction "move $sp, $fp" at this location.
    BuildMI(MBB, I, DL, TII.get(MOVE), SP).addReg(FP).addReg(ZERO);
  }

  if (MipsFI->callsEhReturn()) {
    const TargetRegisterClass *RC =
        ABI.ArePtrs64bit() ? &Mips::GPR64RegClass : &Mips::GPR32RegClass;

    // Find first instruction that restores a callee-saved register.
    MachineBasicBlock::iterator I = MBBI;
    for (unsigned i = 0; i < MFI.getCalleeSavedInfo().size(); ++i)
      --I;

    // Insert instructions that restore eh data registers.
    for (int J = 0; J < 4; ++J) {
      TII.loadRegFromStackSlot(MBB, I, ABI.GetEhDataReg(J),
                               MipsFI->getEhDataRegFI(J), RC, Register());
    }
  }

  if (MF.getFunction().hasFnAttribute("interrupt"))
    emitInterruptEpilogueStub(MF, MBB);

  // Get the number of bytes from FrameInfo
  uint64_t StackSize = MFI.getStackSize();

  if (!StackSize)
    return;

  // The NT unwinder reverse-executes `move $fp, $sp` at every epilogue PC
  // before `jr $ra`, so the reload of $fp, restored last, must immediately
  // precede the return, whose delay slot releases the frame. When the frame
  // is too large for 16-bit offsets, form the frame size and the incoming $sp
  // first so that nothing else follows the reload.
  if (STI.getTargetTriple().isOSWindows() && !ABI.IsN64() && hasFP(MF) &&
      !IsFuncletReturn && !isInt<16>(StackSize) && MBBI != MBB.begin()) {
    MachineBasicBlock::iterator FPLoad = std::prev(MBBI);
    if (FPLoad->getOpcode() == Mips::LW && FPLoad->getOperand(0).isReg() &&
        FPLoad->getOperand(0).getReg() == FP && FPLoad->getOperand(1).isFI()) {
      int64_t FPSlot = MFI.getObjectOffset(FPLoad->getOperand(1).getIndex()) +
                       FPLoad->getOperand(2).getImm();
      FPLoad->eraseFromParent();
      unsigned Size = TII.loadImmediate(StackSize, MBB, MBBI, DL, nullptr);
      Register CFA =
          MF.getRegInfo().createVirtualRegister(&Mips::GPR32RegClass);
      BuildMI(MBB, MBBI, DL, TII.get(Mips::ADDu), CFA).addReg(SP).addReg(Size);
      BuildMI(MBB, MBBI, DL, TII.get(Mips::LW), FP)
          .addReg(CFA, RegState::Kill)
          .addImm(FPSlot);
      BuildMI(MBB, MBBI, DL, TII.get(Mips::ADDu), SP)
          .addReg(SP)
          .addReg(Size, RegState::Kill);
      return;
    }
  }

  // A funclet's frame is released in the delay slot of the `jr $ra` that
  // MipsAsmPrinter emits for CATCHRET and CLEANUPRET, after a catch funclet
  // has formed its continuation address.
  if (IsFuncletReturn && STI.getTargetTriple().isOSWindows())
    return;

  // Adjust stack.
  TII.adjustStackPtr(SP, StackSize, MBB, MBBI);
}

void MipsSEFrameLowering::emitInterruptEpilogueStub(
    MachineFunction &MF, MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();
  DebugLoc DL = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  // Perform ISR handling like GCC
  const TargetRegisterClass *PtrRC = &Mips::GPR32RegClass;

  // Disable Interrupts.
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::DI), Mips::R0);
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::EHB));

  // Restore EPC
  STI.getInstrInfo()->loadRegFromStackSlot(
      MBB, MBBI, Mips::R27, MipsFI->getISRRegFI(0), PtrRC, Register());
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::MTC0), Mips::COP014)
      .addReg(Mips::R27);

  // Restore Status
  STI.getInstrInfo()->loadRegFromStackSlot(
      MBB, MBBI, Mips::R27, MipsFI->getISRRegFI(1), PtrRC, Register());
  BuildMI(MBB, MBBI, DL, STI.getInstrInfo()->get(Mips::MTC0), Mips::COP012)
      .addReg(Mips::R27);
}

StackOffset
MipsSEFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                            Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  MipsABIInfo ABI = STI.getABI();

  if (MFI.isFixedObjectIndex(FI))
    FrameReg = hasFP(MF) ? ABI.GetFramePtr() : ABI.GetStackPtr();
  else
    FrameReg = hasBP(MF) ? ABI.GetBasePtr() : ABI.GetStackPtr();

  return StackOffset::getFixed(MFI.getObjectOffset(FI) + MFI.getStackSize() -
                               getOffsetOfLocalArea() +
                               MFI.getOffsetAdjustment());
}

bool MipsSEFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *STI.getInstrInfo();

  for (const CalleeSavedInfo &I : CSI) {
    // Add the callee-saved register as live-in. Do not add if the register is
    // RA and return address is taken, because it has already been added in
    // method MipsTargetLowering::lowerRETURNADDR.
    // It's killed at the spill, unless the register is RA and return address
    // is taken.
    MCRegister Reg = I.getReg();
    bool IsRAAndRetAddrIsTaken = (Reg == Mips::RA || Reg == Mips::RA_64)
        && MF->getFrameInfo().isReturnAddressTaken();
    if (!IsRAAndRetAddrIsTaken)
      MBB.addLiveIn(Reg);

    // ISRs require HI/LO to be spilled into kernel registers to be then
    // spilled to the stack frame.
    bool IsLOHI = (Reg == Mips::LO0 || Reg == Mips::LO0_64 ||
                   Reg == Mips::HI0 || Reg == Mips::HI0_64);
    const Function &Func = MBB.getParent()->getFunction();
    if (IsLOHI && Func.hasFnAttribute("interrupt")) {
      DebugLoc DL = MI->getDebugLoc();

      unsigned Op = 0;
      if (!STI.getABI().ArePtrs64bit()) {
        Op = (Reg == Mips::HI0) ? Mips::MFHI : Mips::MFLO;
        Reg = Mips::R26;
      } else {
        Op = (Reg == Mips::HI0) ? Mips::MFHI64 : Mips::MFLO64;
        Reg = Mips::R26_64;
      }
      BuildMI(MBB, MI, DL, TII.get(Op), Mips::R26)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    // Insert the spill to the stack frame. On Windows, mark it as part of the
    // prologue: frame index elimination addresses it from the intermediate
    // $sp of a two-step allocation (see emitPrologue).
    bool IsKill = !IsRAAndRetAddrIsTaken;
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.storeRegToStackSlot(MBB, MI, Reg, IsKill, I.getFrameIdx(), RC,
                            Register());
    if (STI.getTargetTriple().isOSWindows())
      std::prev(MI)->setFlag(MachineInstr::FrameSetup);
  }

  return true;
}

bool MipsSEFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (!STI.getTargetTriple().isOSWindows() ||
      llvm::any_of(CSI,
                   [](const CalleeSavedInfo &I) { return I.isSpilledToReg(); }))
    return false;

  // The NT unwinder treats every PC before the final `jr $ra` as body code
  // and reverse-executes `move $fp, $sp`, which takes $sp from $fp. Reload
  // $fp last so that it still holds this frame's value at every such PC.
  const MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  const MCRegister FP = STI.getABI().GetFramePtr();
  const CalleeSavedInfo *FPSave = nullptr;
  auto Restore = [&](const CalleeSavedInfo &I) {
    MCRegister Reg = I.getReg();
    TII.loadRegFromStackSlot(MBB, MI, Reg, I.getFrameIdx(),
                             TRI->getMinimalPhysRegClass(Reg), Register());
  };
  for (const CalleeSavedInfo &I : reverse(CSI)) {
    if (hasFP(MF) && I.getReg() == FP) {
      FPSave = &I;
      continue;
    }
    Restore(I);
  }
  if (FPSave)
    Restore(*FPSave);
  return true;
}

bool
MipsSEFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // Reserve call frame if the size of the maximum call frame fits into 16-bit
  // immediate field and there are no variable sized objects on the stack.
  // Make sure the second register scavenger spill slot can be accessed with one
  // instruction.
  return isInt<16>(MFI.getMaxCallFrameSize() + getStackAlignment()) &&
    !MFI.hasVarSizedObjects();
}

/// Mark \p Reg and all registers aliasing it in the bitset.
static void setAliasRegs(MachineFunction &MF, BitVector &SavedRegs,
                         unsigned Reg) {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  for (MCRegAliasIterator AI(Reg, TRI, true); AI.isValid(); ++AI)
    SavedRegs.set(*AI);
}

void MipsSEFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                               BitVector &SavedRegs,
                                               RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();
  MipsABIInfo ABI = STI.getABI();
  unsigned RA = ABI.IsN64() ? Mips::RA_64 : Mips::RA;
  unsigned FP = ABI.GetFramePtr();
  unsigned BP = ABI.GetBasePtr();

  // Mark $ra and $fp as used if function has dedicated frame pointer.
  if (hasFP(MF)) {
    setAliasRegs(MF, SavedRegs, RA);
    setAliasRegs(MF, SavedRegs, FP);
  }
  // Mark $s7 as used if function has dedicated base pointer.
  if (hasBP(MF))
    setAliasRegs(MF, SavedRegs, BP);

  // Create spill slots for eh data registers if function calls eh_return.
  if (MipsFI->callsEhReturn())
    MipsFI->createEhDataRegsFI(MF);

  // Create spill slots for Coprocessor 0 registers if function is an ISR.
  if (MipsFI->isISR())
    MipsFI->createISRRegFI(MF);

  // Expand pseudo instructions which load, store or copy accumulators.
  // Add an emergency spill slot if a pseudo was expanded.
  if (ExpandPseudo(MF).expand()) {
    // The spill slot should be half the size of the accumulator. If target have
    // general-purpose registers 64 bits wide, it should be 64-bit, otherwise
    // it should be 32-bit.
    const TargetRegisterClass &RC = STI.isGP64bit() ?
      Mips::GPR64RegClass : Mips::GPR32RegClass;
    int FI = MF.getFrameInfo().CreateSpillStackObject(TRI->getSpillSize(RC),
                                                      TRI->getSpillAlign(RC));
    RS->addScavengingFrameIndex(FI);
  }

  // Set scavenging frame index if necessary.
  uint64_t MaxSPOffset = estimateStackSize(MF);

  // MSA has a minimum offset of 10 bits signed. If there is a variable
  // sized object on the stack, the estimation cannot account for it.
  if (isIntN(STI.hasMSA() ? 10 : 16, MaxSPOffset) &&
      !MF.getFrameInfo().hasVarSizedObjects())
    return;

  const TargetRegisterClass &RC =
      ABI.ArePtrs64bit() ? Mips::GPR64RegClass : Mips::GPR32RegClass;
  int FI = MF.getFrameInfo().CreateSpillStackObject(TRI->getSpillSize(RC),
                                                    TRI->getSpillAlign(RC));
  RS->addScavengingFrameIndex(FI);
}

const MipsFrameLowering *
llvm::createMipsSEFrameLowering(const MipsSubtarget &ST) {
  return new MipsSEFrameLowering(ST);
}
