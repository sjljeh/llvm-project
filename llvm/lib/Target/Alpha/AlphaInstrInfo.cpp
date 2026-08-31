//===- AlphaInstrInfo.cpp - Alpha Instruction Information -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Alpha implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "Alpha.h"
#include "AlphaInstrInfo.h"
#include "AlphaMachineFunctionInfo.h"
#include "AlphaSubtarget.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "AlphaGenInstrInfo.inc"
using namespace llvm;

AlphaInstrInfo::AlphaInstrInfo(const AlphaSubtarget &STI)
  : AlphaGenInstrInfo(STI, RI, Alpha::ADJUSTSTACKUP, Alpha::ADJUSTSTACKDOWN),
    RI(*this) {
}


Register
AlphaInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                    int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case Alpha::LDL:
  case Alpha::LDQ:
  case Alpha::LDBU:
  case Alpha::LDWU:
  case Alpha::LDS:
  case Alpha::LDT:
    if (MI.getOperand(1).isFI()) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }
  return 0;
}

Register
AlphaInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                   int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case Alpha::STL:
  case Alpha::STQ:
  case Alpha::STB:
  case Alpha::STW:
  case Alpha::STS:
  case Alpha::STT:
    if (MI.getOperand(1).isFI()) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }
  return 0;
}

static bool isAlphaIntCondCode(unsigned Opcode) {
  switch (Opcode) {
  case Alpha::BEQ: 
  case Alpha::BNE: 
  case Alpha::BGE: 
  case Alpha::BGT: 
  case Alpha::BLE: 
  case Alpha::BLT: 
  case Alpha::BLBC: 
  case Alpha::BLBS:
    return true;
  default:
    return false;
  }
}

static bool isAlphaFloatCondCode(unsigned Opcode) {
  switch (Opcode) {
  case Alpha::FBEQ:
  case Alpha::FBNE:
  case Alpha::FBGE:
  case Alpha::FBGT:
  case Alpha::FBLE:
  case Alpha::FBLT:
    return true;
  default:
    return false;
  }
}

static bool isAlphaCondCode(unsigned Opcode) {
  return isAlphaIntCondCode(Opcode) || isAlphaFloatCondCode(Opcode);
}

static bool getCondBranchInfo(const MachineInstr &MI, MachineBasicBlock *&TBB,
                              SmallVectorImpl<MachineOperand> &Cond) {
  switch (MI.getOpcode()) {
  case Alpha::COND_BRANCH_I:
  case Alpha::COND_BRANCH_F:
    TBB = MI.getOperand(2).getMBB();
    Cond.push_back(MI.getOperand(0));
    Cond.push_back(MI.getOperand(1));
    return true;
  default:
    if (!isAlphaCondCode(MI.getOpcode()))
      return false;
    TBB = MI.getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(MI.getOpcode()));
    Cond.push_back(MI.getOperand(0));
    return true;
  }
}

unsigned AlphaInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  assert(TBB && "InsertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0) && 
         "Alpha branch conditions have two components!");

  auto BuildCondBranch = [&](ArrayRef<MachineOperand> BranchCond,
                             MachineBasicBlock *Target) {
    BuildMI(&MBB, DL, get(BranchCond[0].getImm()))
        .addReg(BranchCond[1].getReg())
        .addMBB(Target);
  };

  // One-way branch.
  if (FBB == 0) {
    MachineBasicBlock *Fallthrough = MBB.getNextNode();
    if (TBB == Fallthrough)
      return 0;

    if (Cond.empty())   // Unconditional branch
      BuildMI(&MBB, DL, get(Alpha::BR)).addMBB(TBB);
    else                // Conditional branch
      BuildCondBranch(Cond, TBB);
    return 1;
  }

  if (Cond.empty()) {
    BuildMI(&MBB, DL, get(Alpha::BR)).addMBB(TBB);
    return 1;
  }

  MachineBasicBlock *Fallthrough = MBB.getNextNode();
  if (FBB == Fallthrough) {
    BuildCondBranch(Cond, TBB);
    return 1;
  }

  if (TBB == Fallthrough) {
    SmallVector<MachineOperand, 2> RevCond(Cond.begin(), Cond.end());
    reverseBranchCondition(RevCond);
    BuildCondBranch(RevCond, FBB);
    return 1;
  }
  
  // Two-way Conditional Branch.
  BuildCondBranch(Cond, TBB);
  BuildMI(&MBB, DL, get(Alpha::BR)).addMBB(FBB);
  return 2;
}

void AlphaInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MI,
                                 const DebugLoc &DL,
                                 Register DestReg, Register SrcReg,
                                 bool KillSrc, bool RenamableDest,
                                 bool RenamableSrc) const {
  if (Alpha::GPRCRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(Alpha::BISr), DestReg)
      .addReg(SrcReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
  } else if (Alpha::F4RCRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(Alpha::CPYSS), DestReg)
      .addReg(SrcReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
  } else if (Alpha::F8RCRegClass.contains(DestReg, SrcReg)) {
    BuildMI(MBB, MI, DL, get(Alpha::CPYST), DestReg)
      .addReg(SrcReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
  } else {
    llvm_unreachable("Attempt to copy register that is not GPR or FPR");
  }
}

void
AlphaInstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MI,
                                     Register SrcReg, bool isKill, int FrameIdx,
                                     const TargetRegisterClass *RC,
                                     Register VReg,
                                     MachineInstr::MIFlag Flags) const {
  //cerr << "Trying to store " << getPrettyName(SrcReg) << " to "
  //     << FrameIdx << "\n";
  //BuildMI(MBB, MI, Alpha::WTF, 0).addReg(SrcReg);

  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  if (RC == &Alpha::F4RCRegClass)
    BuildMI(MBB, MI, DL, get(Alpha::STS))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIdx).addReg(Alpha::F31)
      .setMIFlag(Flags);
  else if (RC == &Alpha::F8RCRegClass)
    BuildMI(MBB, MI, DL, get(Alpha::STT))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIdx).addReg(Alpha::F31)
      .setMIFlag(Flags);
  else if (RC == &Alpha::GPRCRegClass)
    BuildMI(MBB, MI, DL, get(Alpha::STQ))
      .addReg(SrcReg, getKillRegState(isKill))
      .addFrameIndex(FrameIdx).addReg(Alpha::F31)
      .setMIFlag(Flags);
  else
    llvm_unreachable("Unhandled register class");
}

void
AlphaInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MI,
                                     Register DestReg, int FrameIdx,
                                     const TargetRegisterClass *RC,
                                     Register VReg,
                                     unsigned SubReg,
                                     MachineInstr::MIFlag Flags) const {
  //cerr << "Trying to load " << getPrettyName(DestReg) << " to "
  //     << FrameIdx << "\n";
  DebugLoc DL;
  if (MI != MBB.end()) DL = MI->getDebugLoc();

  if (RC == &Alpha::F4RCRegClass)
    BuildMI(MBB, MI, DL, get(Alpha::LDS), DestReg)
      .addFrameIndex(FrameIdx).addReg(Alpha::F31)
      .setMIFlag(Flags);
  else if (RC == &Alpha::F8RCRegClass)
    BuildMI(MBB, MI, DL, get(Alpha::LDT), DestReg)
      .addFrameIndex(FrameIdx).addReg(Alpha::F31)
      .setMIFlag(Flags);
  else if (RC == &Alpha::GPRCRegClass)
    BuildMI(MBB, MI, DL, get(Alpha::LDQ), DestReg)
      .addFrameIndex(FrameIdx).addReg(Alpha::F31)
      .setMIFlag(Flags);
  else
    llvm_unreachable("Unhandled register class");
}

static unsigned AlphaRevCondCode(unsigned Opcode) {
  switch (Opcode) {
  case Alpha::BEQ: return Alpha::BNE;
  case Alpha::BNE: return Alpha::BEQ;
  case Alpha::BGE: return Alpha::BLT;
  case Alpha::BGT: return Alpha::BLE;
  case Alpha::BLE: return Alpha::BGT;
  case Alpha::BLT: return Alpha::BGE;
  case Alpha::BLBC: return Alpha::BLBS;
  case Alpha::BLBS: return Alpha::BLBC;
  case Alpha::FBEQ: return Alpha::FBNE;
  case Alpha::FBNE: return Alpha::FBEQ;
  case Alpha::FBGE: return Alpha::FBLT;
  case Alpha::FBGT: return Alpha::FBLE;
  case Alpha::FBLE: return Alpha::FBGT;
  case Alpha::FBLT: return Alpha::FBGE;
  default:
    llvm_unreachable("Unknown opcode");
  }
  return 0; // Not reached
}

// Branch analysis.
bool AlphaInstrInfo::analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::iterator I = MBB.end();
  if (I == MBB.begin())
    return false;
  --I;
  while (I->isDebugValue()) {
    if (I == MBB.begin())
      return false;
    --I;
  }
  if (!isUnpredicatedTerminator(*I))
    return false;

  // Get the last instruction in the block.
  MachineInstr *LastInst = &*I;
  
  // If there is only one terminator instruction, process it.
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (LastInst->getOpcode() == Alpha::BR) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    } else if (getCondBranchInfo(*LastInst, TBB, Cond)) {
      // Block ends with fall-through condbranch.
      return false;
    }
    // Otherwise, don't know what this is.
    return true;
  }
  
  // Get the instruction before it if it's a terminator.
  MachineInstr *SecondLastInst = &*I;

  // If there are three terminators, we don't know what sort of block this is.
  if (SecondLastInst && I != MBB.begin() &&
      isUnpredicatedTerminator(*--I))
    return true;
  
  // If the block ends with Alpha::BR and Alpha::COND_BRANCH_*, handle it.
  if (LastInst->getOpcode() == Alpha::BR &&
      getCondBranchInfo(*SecondLastInst, TBB, Cond)) {
    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }
  
  // If the block ends with two Alpha::BRs, handle it.  The second one is not
  // executed, so remove it.
  if (SecondLastInst->getOpcode() == Alpha::BR && 
      LastInst->getOpcode() == Alpha::BR) {
    TBB = SecondLastInst->getOperand(0).getMBB();
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return false;
  }

  // Otherwise, can't handle this.
  return true;
}

unsigned AlphaInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.end();
  if (I == MBB.begin()) return 0;
  --I;
  while (I->isDebugValue()) {
    if (I == MBB.begin())
      return 0;
    --I;
  }
  if (I->getOpcode() != Alpha::BR &&
      I->getOpcode() != Alpha::COND_BRANCH_I &&
      I->getOpcode() != Alpha::COND_BRANCH_F &&
      !isAlphaCondCode(I->getOpcode()))
    return 0;
  
  // Remove the branch.
  I->eraseFromParent();
  
  I = MBB.end();

  if (I == MBB.begin()) return 1;
  --I;
  if (I->getOpcode() != Alpha::COND_BRANCH_I &&
      I->getOpcode() != Alpha::COND_BRANCH_F &&
      !isAlphaCondCode(I->getOpcode()))
    return 1;
  
  // Remove the branch.
  I->eraseFromParent();
  return 2;
}

void AlphaInstrInfo::insertNoop(MachineBasicBlock &MBB, 
                                MachineBasicBlock::iterator MI) const {
  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(Alpha::UNOP));
}

bool AlphaInstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 2 && "Invalid Alpha branch opcode!");
  Cond[0].setImm(AlphaRevCondCode(Cond[0].getImm()));
  return false;
}

/// getGlobalBaseReg - Return a virtual register initialized with the
/// the global base register value. Output instructions required to
/// initialize the register in the function entry block, if necessary.
///
unsigned AlphaInstrInfo::getGlobalBaseReg(MachineFunction *MF) const {
  AlphaMachineFunctionInfo *AlphaFI = MF->getInfo<AlphaMachineFunctionInfo>();
  unsigned GlobalBaseReg = AlphaFI->getGlobalBaseReg();
  if (GlobalBaseReg != 0)
    return GlobalBaseReg;

  // Insert the set of GlobalBaseReg into the first MBB of the function
  MachineBasicBlock &FirstMBB = MF->front();
  MachineBasicBlock::iterator MBBI = FirstMBB.begin();
  MachineRegisterInfo &RegInfo = MF->getRegInfo();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

  GlobalBaseReg = RegInfo.createVirtualRegister(&Alpha::GPRCRegClass);
  BuildMI(FirstMBB, MBBI, DebugLoc(), TII->get(TargetOpcode::COPY),
          GlobalBaseReg).addReg(Alpha::R29);
  RegInfo.addLiveIn(Alpha::R29);

  AlphaFI->setGlobalBaseReg(GlobalBaseReg);
  return GlobalBaseReg;
}

/// getGlobalRetAddr - Return a virtual register initialized with the
/// the global base register value. Output instructions required to
/// initialize the register in the function entry block, if necessary.
///
unsigned AlphaInstrInfo::getGlobalRetAddr(MachineFunction *MF) const {
  AlphaMachineFunctionInfo *AlphaFI = MF->getInfo<AlphaMachineFunctionInfo>();
  unsigned GlobalRetAddr = AlphaFI->getGlobalRetAddr();
  if (GlobalRetAddr != 0)
    return GlobalRetAddr;

  // Insert the set of GlobalRetAddr into the first MBB of the function
  MachineBasicBlock &FirstMBB = MF->front();
  MachineBasicBlock::iterator MBBI = FirstMBB.begin();
  MachineRegisterInfo &RegInfo = MF->getRegInfo();
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

  GlobalRetAddr = RegInfo.createVirtualRegister(&Alpha::GPRCRegClass);
  BuildMI(FirstMBB, MBBI, DebugLoc(), TII->get(TargetOpcode::COPY),
          GlobalRetAddr)
      .addReg(Alpha::R26)
      .setMIFlag(MachineInstr::FrameSetup);
  RegInfo.addLiveIn(Alpha::R26);

  AlphaFI->setGlobalRetAddr(GlobalRetAddr);
  return GlobalRetAddr;
}
