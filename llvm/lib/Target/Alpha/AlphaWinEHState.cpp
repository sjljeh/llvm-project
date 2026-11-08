//===-- AlphaWinEHState.cpp - Windows Alpha C++ EH state tracking ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The NT Alpha C++ runtime (__CxxFrameHandler) has no IP-to-state map. It reads
// the current EH state from a record { ULONG State; PVOID Rfp; } in the
// function's frame, at EstablisherFrame + FuncInfo.EHContextDelta.
// Compiler-generated code stores the real
// frame pointer once on entry (AlphaFrameLowering::emitPrologue) and the state
// number before each call site that can throw (this pass).
//
// Modeled on X86WinEHState: the record is an entry-block alloca marked with
// !alpha.ehcontext, and stores are elided where the state is already known.
//
//===----------------------------------------------------------------------===//

#include "Alpha.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include <deque>

using namespace llvm;

#define DEBUG_TYPE "alpha-winehstate"

namespace {
constexpr int OverdefinedState = INT_MIN;
constexpr int ParentBaseState = -1;

class AlphaWinEHState : public FunctionPass {
public:
  static char ID;
  AlphaWinEHState() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;
  StringRef getPassName() const override {
    return "Windows Alpha C++ EH state stores";
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
  }

private:
  void insertStateStore(Instruction *IP, int State);
  int getBaseStateForBB(DenseMap<BasicBlock *, ColorVector> &BlockColors,
                        WinEHFuncInfo &FuncInfo, BasicBlock *BB);
  int getStateForCall(DenseMap<BasicBlock *, ColorVector> &BlockColors,
                      WinEHFuncInfo &FuncInfo, CallBase &Call);

  AllocaInst *EHContext = nullptr;
};
} // end anonymous namespace

char AlphaWinEHState::ID = 0;

FunctionPass *llvm::createAlphaWinEHStatePass() {
  return new AlphaWinEHState();
}

bool llvm::isAlphaWinCXXEHContext(const AllocaInst &AI) {
  return AI.getMetadata("alpha.ehcontext") != nullptr;
}

static bool usesLegacyCxxFrameHandler(const Function &F) {
  if (!F.hasPersonalityFn())
    return false;
  const auto *PerFn =
      dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
  return PerFn && PerFn->getName() == "__CxxFrameHandler" &&
         classifyEHPersonality(PerFn) == EHPersonality::MSVC_CXX;
}

void AlphaWinEHState::insertStateStore(Instruction *IP, int State) {
  IRBuilder<> Builder(IP);
  // Volatile: nothing in this function reads the state; the runtime does.
  Builder.CreateAlignedStore(Builder.getInt32(State), EHContext, Align(4),
                             /*isVolatile=*/true);
}

int AlphaWinEHState::getBaseStateForBB(
    DenseMap<BasicBlock *, ColorVector> &BlockColors, WinEHFuncInfo &FuncInfo,
    BasicBlock *BB) {
  auto &BBColors = BlockColors[BB];
  assert(BBColors.size() == 1 && "multi-color BB not removed by preparation");
  BasicBlock *FuncletEntryBB = BBColors.front();
  if (auto *FuncletPad =
          dyn_cast<FuncletPadInst>(FuncletEntryBB->getFirstNonPHIIt())) {
    auto BaseStateI = FuncInfo.FuncletBaseStateMap.find(FuncletPad);
    if (BaseStateI != FuncInfo.FuncletBaseStateMap.end())
      return BaseStateI->second;
  }
  return ParentBaseState;
}

int AlphaWinEHState::getStateForCall(
    DenseMap<BasicBlock *, ColorVector> &BlockColors, WinEHFuncInfo &FuncInfo,
    CallBase &Call) {
  if (auto *II = dyn_cast<InvokeInst>(&Call)) {
    assert(FuncInfo.InvokeStateMap.count(II) && "invoke has no state!");
    return FuncInfo.InvokeStateMap[II];
  }
  // A call without an unwind edge has nothing to unwind in this funclet.
  return getBaseStateForBB(BlockColors, FuncInfo, Call.getParent());
}

// The state shared by the ends of all of BB's predecessors.
static int getPredState(DenseMap<BasicBlock *, int> &FinalStates, Function &F,
                        BasicBlock *BB) {
  if (&F.getEntryBlock() == BB)
    return ParentBaseState;
  // An EH pad is entered from the runtime, which leaves the parent's state at
  // the try block's enclosing state.
  if (BB->isEHPad())
    return OverdefinedState;

  int CommonState = OverdefinedState;
  for (BasicBlock *PredBB : predecessors(BB)) {
    auto PredEndState = FinalStates.find(PredBB);
    if (PredEndState == FinalStates.end())
      return OverdefinedState;
    // A catch funclet may have stored its own states before returning here.
    if (isa<CatchReturnInst>(PredBB->getTerminator()))
      return OverdefinedState;
    int PredState = PredEndState->second;
    if (CommonState == OverdefinedState)
      CommonState = PredState;
    if (CommonState != PredState)
      return OverdefinedState;
  }
  return CommonState;
}

// The state shared by the starts of all of BB's successors.
static int getSuccState(DenseMap<BasicBlock *, int> &InitialStates,
                        BasicBlock *BB) {
  if (isa<CatchReturnInst>(BB->getTerminator()))
    return OverdefinedState;

  int CommonState = OverdefinedState;
  for (BasicBlock *SuccBB : successors(BB)) {
    auto SuccStartState = InitialStates.find(SuccBB);
    if (SuccStartState == InitialStates.end())
      return OverdefinedState;
    if (SuccBB->isEHPad())
      return OverdefinedState;
    int SuccState = SuccStartState->second;
    if (CommonState == OverdefinedState)
      CommonState = SuccState;
    if (CommonState != SuccState)
      return OverdefinedState;
  }
  return CommonState;
}

static bool isStateStoreNeeded(CallBase &Call) { return !Call.doesNotThrow(); }

bool AlphaWinEHState::runOnFunction(Function &F) {
  if (F.hasAvailableExternallyLinkage() || !usesLegacyCxxFrameHandler(F))
    return false;

  bool HasPads = false, Diagnosed = false;
  for (BasicBlock &BB : F) {
    if (!BB.isEHPad())
      continue;
    HasPads = true;
    // The runtime calls a catch as a nested function with no catch depth:
    // a try block
    // inside a funclet would need the funclet to carry its own FuncInfo and
    // EH context record, and catch objects in the funclet's frame.
    // Diagnose it; the tables below are still emitted, but wrong.
    if (auto *CS = dyn_cast<CatchSwitchInst>(BB.getFirstNonPHIIt()))
      if (!isa<ConstantTokenNone>(CS->getParentPad()) && !Diagnosed) {
        F.getContext().diagnose(DiagnosticInfoUnsupported(
            F, "Windows Alpha C++ EH: try blocks inside catch or cleanup "
               "handlers are not supported"));
        Diagnosed = true;
      }
  }
  if (!HasPads)
    return false;

  // the { ULONG State; PVOID Rfp; } EH context record at the top of the frame.
  // PEI allocates it first (WinEHFuncInfo::EHRegNodeFrameIndex).
  LLVMContext &Ctx = F.getContext();
  IRBuilder<> Builder(&*F.getEntryBlock().getFirstInsertionPt());
  EHContext = Builder.CreateAlloca(ArrayType::get(Builder.getInt32Ty(), 2),
                                   nullptr, "ehcontext");
  EHContext->setAlignment(Align(8));
  EHContext->setMetadata("alpha.ehcontext", MDNode::get(Ctx, {}));
  insertStateStore(EHContext->getNextNode(), ParentBaseState);

  WinEHFuncInfo FuncInfo;
  calculateWinCXXEHStateNumbers(&F, FuncInfo);

  DenseMap<BasicBlock *, ColorVector> BlockColors = colorEHFunclets(F);
  ReversePostOrderTraversal<Function *> RPOT(&F);

  // InitialStates yields the state of the first call-site for a BasicBlock.
  DenseMap<BasicBlock *, int> InitialStates;
  // FinalStates yields the state of the last call-site for a BasicBlock.
  DenseMap<BasicBlock *, int> FinalStates;
  std::deque<BasicBlock *> Worklist;
  for (BasicBlock *BB : RPOT) {
    int InitialState = OverdefinedState;
    int FinalState = OverdefinedState;
    if (&F.getEntryBlock() == BB)
      InitialState = FinalState = ParentBaseState;
    for (Instruction &I : *BB) {
      auto *Call = dyn_cast<CallBase>(&I);
      if (!Call || !isStateStoreNeeded(*Call))
        continue;
      int State = getStateForCall(BlockColors, FuncInfo, *Call);
      if (InitialState == OverdefinedState)
        InitialState = State;
      FinalState = State;
    }
    if (InitialState == OverdefinedState) {
      Worklist.push_back(BB);
      continue;
    }
    InitialStates.insert({BB, InitialState});
    FinalStates.insert({BB, FinalState});
  }

  // Infer the states of blocks without call sites from their predecessors.
  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.front();
    Worklist.pop_front();
    if (InitialStates.count(BB) != 0)
      continue;
    int PredState = getPredState(FinalStates, F, BB);
    if (PredState == OverdefinedState)
      continue;
    InitialStates.insert({BB, PredState});
    FinalStates.insert({BB, PredState});
    for (BasicBlock *SuccBB : successors(BB))
      Worklist.push_back(SuccBB);
  }

  // Hoist stores from successors that agree on their initial state.
  for (BasicBlock *BB : RPOT) {
    int SuccState = getSuccState(InitialStates, BB);
    if (SuccState == OverdefinedState)
      continue;
    FinalStates.insert({BB, SuccState});
  }

  for (BasicBlock *BB : RPOT) {
    // An exception leaving a cleanup is fatal;
    // the runtime tracks the state while it unwinds.
    BasicBlock *FuncletEntryBB = BlockColors[BB].front();
    if (isa<CleanupPadInst>(FuncletEntryBB->getFirstNonPHIIt()))
      continue;

    int PrevState = getPredState(FinalStates, F, BB);
    for (Instruction &I : *BB) {
      auto *Call = dyn_cast<CallBase>(&I);
      if (!Call || !isStateStoreNeeded(*Call))
        continue;
      int State = getStateForCall(BlockColors, FuncInfo, *Call);
      if (State != PrevState)
        insertStateStore(&I, State);
      PrevState = State;
    }

    auto EndState = FinalStates.find(BB);
    if (EndState != FinalStates.end() && EndState->second != PrevState)
      insertStateStore(BB->getTerminator(), EndState->second);
  }

  EHContext = nullptr;
  return true;
}
