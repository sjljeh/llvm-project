//===- NTUWRuleTracker.cpp - NTUW rule synthesis --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/NTUWRuleTracker.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;
using namespace llvm::NTRISCWinEH;

NTUWRuleTracker::NTUWRuleTracker(AsmPrinter &AP, Machine M,
                                 uint8_t FramePointer)
    : AP(AP), M(M), Info(getMachineInfo(M)), FramePointer(FramePointer) {
  // The registers whose caller values the rules describe: the integer and
  // floating-point nonvolatiles, the return-address register, and on
  // PowerPC LR and CR.
  for (unsigned R = 0; R != 32; ++R)
    if (isIntegerNonvolatile(M, R) || Info.ReturnRegister == R)
      Preserved.push_back({RegClass::GPR, uint8_t(R)});
  for (unsigned R = 0; R != 32; ++R)
    if (isFloatingNonvolatile(M, R))
      Preserved.push_back({RegClass::FPR, uint8_t(R)});
  if (Info.HasSPRs) {
    Preserved.push_back({RegClass::SPR, SPR_LR});
    Preserved.push_back({RegClass::SPR, SPR_CR});
  }
}

NTUWRuleTracker::~NTUWRuleTracker() = default;

void NTUWRuleTracker::endModule() {
  if (AP.OutStreamer && AP.OutStreamer->getInstructionObserver() == this)
    AP.OutStreamer->setInstructionObserver(nullptr);
}

void NTUWRuleTracker::fail(const Twine &Message) const {
  std::string Name = MF ? MF->getName().str() : std::string("<unknown>");
  report_fatal_error(Twine("NTUW unwind synthesis failed in ") + Name + ": " +
                     Message);
}

NTUWRuleTracker::Value NTUWRuleTracker::read(const State &S, Reg R) const {
  switch (R.Class) {
  case RegClass::GPR:
    if (Info.ZeroRegister == R.Num) {
      Value V;
      V.K = Value::Const;
      return V;
    }
    return S.GPR[R.Num];
  case RegClass::FPR:
    return S.FPR[R.Num];
  case RegClass::SPR:
    return S.SPR[R.Num];
  }
  llvm_unreachable("bad register class");
}

void NTUWRuleTracker::write(State &S, Reg R, Value V) {
  switch (R.Class) {
  case RegClass::GPR:
    if (Info.ZeroRegister != R.Num)
      S.GPR[R.Num] = V;
    return;
  case RegClass::FPR:
    S.FPR[R.Num] = V;
    return;
  case RegClass::SPR:
    S.SPR[R.Num] = V;
    return;
  }
}

static unsigned naturalSize(const MachineInfo &Info,
                            NTUWRuleTracker::RegClass Class) {
  switch (Class) {
  case NTUWRuleTracker::RegClass::GPR:
    return Info.GPRSize;
  case NTUWRuleTracker::RegClass::FPR:
    return 8;
  case NTUWRuleTracker::RegClass::SPR:
    return 4;
  }
  return 0;
}

void NTUWRuleTracker::apply(State &S, const Effect &E) {
  Value Result;
  auto IsStackAddress = [&](Value Base, int64_t Offset,
                            int64_t &CFAOffset) -> bool {
    if (Base.K != Value::Entry || Base.R.Class != RegClass::GPR ||
        Base.R.Num != Info.StackPointer)
      return false;
    CFAOffset = Base.Off + Offset;
    return true;
  };
  switch (E.K) {
  case Effect::Clobber:
    break;
  case Effect::SetConst:
    Result.K = Value::Const;
    Result.Off = E.Imm;
    break;
  case Effect::AddImm: {
    Value A = read(S, E.Src);
    if (A.K != Value::Unknown) {
      Result = A;
      Result.Off += E.Imm;
    }
    break;
  }
  case Effect::AddReg: {
    Value A = read(S, E.Src), B = read(S, E.Src2);
    if (A.K == Value::Const && B.K != Value::Unknown)
      std::swap(A, B);
    if (B.K == Value::Const && A.K != Value::Unknown) {
      Result = A;
      Result.Off += B.Off;
    }
    break;
  }
  case Effect::SubReg: {
    Value A = read(S, E.Src), B = read(S, E.Src2);
    if (B.K == Value::Const && A.K != Value::Unknown) {
      Result = A;
      Result.Off -= B.Off;
    } else if (A.K == Value::Entry && B.K == Value::Entry && A.R == B.R) {
      Result.K = Value::Const;
      Result.Off = A.Off - B.Off;
    }
    break;
  }
  case Effect::OrImm: {
    Value A = read(S, E.Src);
    if (A.K == Value::Const) {
      Result.K = Value::Const;
      Result.Off = A.Off | E.Imm;
    } else if (E.Imm == 0) {
      Result = A;
    }
    break;
  }
  case Effect::ShlImm: {
    Value A = read(S, E.Src);
    if (A.K == Value::Const) {
      Result.K = Value::Const;
      Result.Off = uint64_t(A.Off) << E.Imm;
    }
    break;
  }
  case Effect::Copy:
    Result = read(S, E.Src);
    break;
  case Effect::LoadBackChain: {
    int64_t Offset;
    if (IsStackAddress(read(S, E.Src), 0, Offset)) {
      auto It = S.Memory.find(Offset);
      if (It != S.Memory.end() && It->second.Size == E.Size)
        Result = It->second.V;
    } else if (CurMode == Mode::Body || CurMode == Mode::Epilogue) {
      Result.K = Value::Entry;
      Result.R = {RegClass::GPR, Info.StackPointer};
    }
    break;
  }
  case Effect::Load: {
    int64_t Offset;
    if (IsStackAddress(read(S, E.Src), E.Imm, Offset) &&
        E.Size == naturalSize(Info, E.Dst.Class)) {
      auto It = S.Memory.find(Offset);
      if (It != S.Memory.end() && It->second.Size == E.Size &&
          (It->second.V.K != Value::Entry ||
           naturalSize(Info, It->second.V.R.Class) == E.Size))
        Result = It->second.V;
    }
    break;
  }
  case Effect::Store:
  case Effect::StoreIndexed: {
    int64_t Offset;
    Value Base = read(S, E.Src2);
    int64_t Displacement = E.Imm;
    if (E.K == Effect::StoreIndexed) {
      Value Index = read(S, E.Src3);
      if (Index.K != Value::Const)
        return;
      Displacement = Index.Off;
    }
    // Stores through other bases are assumed not to alias the frame's save
    // slots, which only the prologue and epilogue address.
    if (!IsStackAddress(Base, Displacement, Offset))
      return;
    for (auto It = S.Memory.begin(); It != S.Memory.end();) {
      if (It->first < Offset + E.Size && Offset < It->first + It->second.Size)
        It = S.Memory.erase(It);
      else
        ++It;
    }
    Value Stored = read(S, E.Src);
    if (E.Size != naturalSize(Info, E.Src.Class))
      Stored = Value();
    S.Memory[Offset] = {Stored, E.Size};
    return;
  }
  }
  Value Entry;
  Entry.K = Value::Entry;
  Entry.R = E.Dst;
  bool &Restored = S.Restored[unsigned(E.Dst.Class)][E.Dst.Num];
  if (Result == Entry) {
    // A reload counts even when the register still held the caller's value.
    if (E.K == Effect::Load || !(read(S, E.Dst) == Entry))
      Restored = true;
  } else {
    Restored = false;
  }
  write(S, E.Dst, Result);
}

void NTUWRuleTracker::clobberVolatiles(State &S) {
  for (unsigned R = 0; R != 32; ++R) {
    if (R == Info.StackPointer || isIntegerNonvolatile(M, R))
      continue;
    write(S, {RegClass::GPR, uint8_t(R)}, Value());
  }
  for (unsigned R = 0; R != 32; ++R)
    if (!isFloatingNonvolatile(M, R))
      S.FPR[R] = Value();
  S.SPR[SPR_LR] = Value();
  S.SPR[SPR_CTR] = Value();
}

bool NTUWRuleTracker::deriveRules(const State &S, Rules &Out,
                                  std::string &Error) const {
  // The CFA: a register holding the CFA plus a constant.
  auto CFABase = [&](unsigned R) -> std::optional<int64_t> {
    if (Info.ZeroRegister == R)
      return std::nullopt;
    const Value &V = S.GPR[R];
    if (V.K == Value::Entry && V.R.Class == RegClass::GPR &&
        V.R.Num == Info.StackPointer)
      return V.Off;
    return std::nullopt;
  };
  std::optional<unsigned> Base;
  if (CFABase(FramePointer) &&
      (CurMode == Mode::Prologue || S.R.CFARegister == FramePointer))
    Base = FramePointer;
  else if (CFABase(S.R.CFARegister))
    Base = S.R.CFARegister;
  else if (CFABase(Info.StackPointer))
    Base = Info.StackPointer;
  else if (CFABase(FramePointer))
    Base = FramePointer;
  else {
    for (unsigned R = 0; R != 32 && !Base; ++R)
      if (isIntegerNonvolatile(M, R) && CFABase(R))
        Base = R;
    for (unsigned R = 0; R != 32 && !Base; ++R)
      if (CFABase(R))
        Base = R;
  }
  if (!Base) {
    Error = "no register holds the CFA";
    return false;
  }
  int64_t Offset = -*CFABase(*Base);
  if (!isInt<32>(Offset)) {
    Error = "the CFA offset does not fit 32 bits";
    return false;
  }
  Out.CFARegister = *Base;
  Out.CFAOffset = static_cast<int32_t>(Offset);

  Out.Regs.resize(Preserved.size());
  for (unsigned I = 0; I != Preserved.size(); ++I) {
    Reg P = Preserved[I];
    Value Entry;
    Entry.K = Value::Entry;
    Entry.R = P;
    bool Live = read(S, P) == Entry;
    if (Live && S.Restored[unsigned(P.Class)][P.Num]) {
      Out.Regs[I] = {};
      continue;
    }
    std::optional<int64_t> SlotOffset;
    const NTRISCWinEH::RegisterRule &Previous =
        S.R.Regs.empty() ? NTRISCWinEH::RegisterRule() : S.R.Regs[I];
    if (Previous.Kind == RuleKind::Saved) {
      auto It = S.Memory.find(Previous.Offset);
      if (It != S.Memory.end() && It->second.V == Entry)
        SlotOffset = Previous.Offset;
    }
    if (!SlotOffset)
      for (const auto &[Offset, Slot] : S.Memory)
        if (Slot.V == Entry) {
          SlotOffset = Offset;
          break;
        }
    if (SlotOffset) {
      if (!isInt<32>(*SlotOffset)) {
        Error = "a save slot's offset does not fit 32 bits";
        return false;
      }
      Out.Regs[I] = {RuleKind::Saved, static_cast<int32_t>(*SlotOffset), 0};
      continue;
    }
    if (Live) {
      Out.Regs[I] = {};
      continue;
    }
    std::optional<unsigned> Source;
    RegClass SourceClass =
        P.Class == RegClass::FPR ? RegClass::FPR : RegClass::GPR;
    for (unsigned R = 0; R != 32 && !Source; ++R)
      if (read(S, {SourceClass, uint8_t(R)}) == Entry)
        Source = R;
    if (Source) {
      Out.Regs[I] = {RuleKind::FromRegister, 0, uint8_t(*Source)};
      continue;
    }
    const char *ClassName = P.Class == RegClass::GPR   ? "GPR"
                            : P.Class == RegClass::FPR ? "FPR"
                                                       : "SPR";
    Error = (Twine("the caller's ") + ClassName + " " + Twine(P.Num) +
             " is neither live nor saved")
                .str();
    return false;
  }
  return true;
}

void NTUWRuleTracker::emitRuleChanges(const Rules &From, const Rules &To) {
  if (From.CFARegister != To.CFARegister || From.CFAOffset != To.CFAOffset)
    Pending.push_back({UOP_SetCFA, To.CFARegister, To.CFAOffset});
  for (unsigned I = 0; I != Preserved.size(); ++I) {
    const NTRISCWinEH::RegisterRule &A =
        From.Regs.empty() ? NTRISCWinEH::RegisterRule() : From.Regs[I];
    const NTRISCWinEH::RegisterRule &B = To.Regs[I];
    if (A == B)
      continue;
    Reg P = Preserved[I];
    unsigned Same, Save, FromReg;
    switch (P.Class) {
    case RegClass::GPR:
      Same = UOP_SameGPR, Save = UOP_SaveGPR, FromReg = UOP_GPRFromGPR;
      break;
    case RegClass::FPR:
      Same = UOP_SameFPR, Save = UOP_SaveFPR, FromReg = UOP_FPRFromFPR;
      break;
    case RegClass::SPR:
      Same = UOP_SameSPR, Save = UOP_SaveSPR, FromReg = UOP_SPRFromGPR;
      break;
    }
    switch (B.Kind) {
    case RuleKind::Unchanged:
      Pending.push_back({Same, P.Num, 0});
      break;
    case RuleKind::Saved:
      Pending.push_back({Save, P.Num, B.Offset});
      break;
    case RuleKind::FromRegister:
      Pending.push_back({FromReg, P.Num, B.Source});
      break;
    }
  }
}

void NTUWRuleTracker::resetFrame(const WinEH::FrameInfo *NewFrame) {
  Frame = NewFrame;
  CurMode = Mode::Prologue;
  Pending.clear();
  EndEpilogueAfterInstruction = false;
  Cur = State();
  for (unsigned R = 0; R != 32; ++R) {
    Cur.GPR[R].K = Value::Entry;
    Cur.GPR[R].R = {RegClass::GPR, uint8_t(R)};
    Cur.FPR[R].K = Value::Entry;
    Cur.FPR[R].R = {RegClass::FPR, uint8_t(R)};
  }
  for (unsigned R = 0; R != NumSPRs; ++R) {
    Cur.SPR[R].K = Value::Entry;
    Cur.SPR[R].R = {RegClass::SPR, uint8_t(R)};
  }
  Cur.R.CFARegister = Info.StackPointer;
  Cur.R.CFAOffset = 0;
  Cur.R.Regs.assign(Preserved.size(), NTRISCWinEH::RegisterRule());
  Body = Cur;
}

void NTUWRuleTracker::emitPending() {
  for (const PendingRule &P : Pending)
    AP.OutStreamer->emitWinCFIStateRule(P.Opcode, P.Register, P.Operand);
  Pending.clear();
}

void NTUWRuleTracker::observeInstruction(const MCInst &Inst,
                                         const MCSubtargetInfo &STI) {
  const WinEH::FrameInfo *F = AP.OutStreamer->getCurrentWinFrame();
  if (!F || F->End) {
    Frame = nullptr;
    CurMode = Mode::Outside;
    return;
  }
  if (F != Frame)
    resetFrame(F);
  // The previous instruction's rules hold from here on.
  emitPending();
  if (CurMode == Mode::Prologue && F->PrologEnd) {
    CurMode = Mode::Body;
    Body = Cur;
  }

  Decoded D;
  if (!decode(Inst, D)) {
    D = Decoded();
    const MCInstrDesc &Desc = AP.TM.getMCInstrInfo()->get(Inst.getOpcode());
    for (unsigned I = 0, E = Desc.getNumDefs();
         I != E && I < Inst.getNumOperands(); ++I)
      if (Inst.getOperand(I).isReg())
        if (std::optional<Reg> R = mapRegister(Inst.getOperand(I).getReg()))
          D.Effects.push_back({Effect::Clobber, *R, {}, {}, {}, 0, 0});
    for (MCPhysReg Def : Desc.implicit_defs())
      if (std::optional<Reg> R = mapRegister(Def))
        D.Effects.push_back({Effect::Clobber, *R, {}, {}, {}, 0, 0});
    D.IsCall = Desc.isCall();
    D.LeavesFunction = Desc.isReturn();
    D.IsBranch = Desc.isBranch() || Desc.isIndirectBranch();
  }

  State Next = Cur;
  for (const Effect &E : D.Effects)
    apply(Next, E);
  if (D.IsCall)
    clobberVolatiles(Next);

  Rules NewRules;
  std::string Error;
  if (!deriveRules(Next, NewRules, Error))
    fail(Error);

  switch (CurMode) {
  case Mode::Outside:
    return;
  case Mode::Prologue:
    emitRuleChanges(Cur.R, NewRules);
    break;
  case Mode::Body:
    if (NewRules == Body.R)
      break;
    // The first instruction that changes the rules starts an epilogue.
    AP.OutStreamer->emitWinCFIBeginEpilogue();
    CurMode = Mode::Epilogue;
    [[fallthrough]];
  case Mode::Epilogue:
    emitRuleChanges(Cur.R, NewRules);
    if (D.LeavesFunction)
      EndEpilogueAfterInstruction = true;
    else if (D.IsBranch && !D.IsCall)
      fail("an epilogue branches before it returns");
    break;
  }
  Next.R = NewRules;
  Cur = std::move(Next);
}

void NTUWRuleTracker::endInstruction() {
  if (CurMode == Mode::Outside)
    return;
  emitPending();
  if (EndEpilogueAfterInstruction) {
    AP.OutStreamer->emitWinCFIEndEpilogue();
    EndEpilogueAfterInstruction = false;
    Cur = Body;
    CurMode = Mode::Body;
  }
}

void NTUWRuleTracker::beginFunction(const MachineFunction *NewMF) {
  MF = NewMF;
  Frame = nullptr;
  CurMode = Mode::Outside;
  Pending.clear();
  EndEpilogueAfterInstruction = false;
}

void NTUWRuleTracker::endFunclet() {
  if (CurMode == Mode::Epilogue)
    fail("an epilogue does not end in a return");
  Frame = nullptr;
  CurMode = Mode::Outside;
}

void NTUWRuleTracker::endFunction(const MachineFunction *) {
  endFunclet();
  MF = nullptr;
}
