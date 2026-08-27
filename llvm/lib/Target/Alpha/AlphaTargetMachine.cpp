//===-- AlphaTargetMachine.cpp - Define TargetMachine for Alpha -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaTargetMachine.h"
#include "Alpha.h"
#include "AlphaMachineFunctionInfo.h"
#include "MCTargetDesc/AlphaMCTargetDesc.h"
#include "TargetInfo/AlphaTargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/MathExtras.h"
#include <cctype>

using namespace llvm;
using namespace llvm::codeview;

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAlphaTarget() {
  RegisterTargetMachine<AlphaTargetMachine> X(getTheAlphaTarget());
}

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static std::unique_ptr<TargetLoweringObjectFile> createTLOF(const Triple &TT) {
  if (TT.isOSBinFormatCOFF())
    return std::make_unique<TargetLoweringObjectFileCOFF>();
  return std::make_unique<TargetLoweringObjectFileELF>();
}

static bool hasTASOFeature(StringRef FS) {
  SmallVector<StringRef, 8> Features;
  FS.split(Features, ',');
  return llvm::is_contained(Features, "+taso");
}

static StringRef getDataLayoutForTriple(const Triple &TT, StringRef FS) {
  if (TT.isOSWindows() && hasTASOFeature(FS))
    return "e-p:32:32-f64:64-n32:64";
  if (TT.isOSWindows())
    return "e-p:64:64-f64:64-n32:64";
  return "e-p:64:64-f128:128:128-n64";
}

AlphaTargetMachine::AlphaTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, getDataLayoutForTriple(TT, FS), TT, CPU, FS,
                               Options, getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(createTLOF(TT)),
      Subtarget(TT, CPU, FS, *this) {
  initAsmInfo();
}

AlphaTargetMachine::~AlphaTargetMachine() = default;

namespace {
class AlphaPassConfig : public TargetPassConfig {
public:
  AlphaPassConfig(AlphaTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    setEnableTailMerge(false);
  }

  AlphaTargetMachine &getAlphaTargetMachine() const {
    return getTM<AlphaTargetMachine>();
  }

  bool addInstSelector() override {
    addPass(createAlphaISelDag(getAlphaTargetMachine()));
    return false;
  }

  void addMachineLateOptimization() override {
    addPass(&MachineLateInstrsCleanupID);
    addPass(createAlphaBranchSelectionPass());
    if (!getAlphaTargetMachine().requiresStructuredCFG())
      addPass(&TailDuplicateLegacyID);
    addPass(&MachineCopyPropagationID);
  }

  void addPreEmitPass() override {
    addPass(createAlphaLLRPPass(getAlphaTargetMachine()));
    addPass(createAlphaBranchSelectionPass());
  }
};
} // end anonymous namespace

TargetPassConfig *AlphaTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new AlphaPassConfig(*this, PM);
}

MachineFunctionInfo *AlphaTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return AlphaMachineFunctionInfo::create<AlphaMachineFunctionInfo>(Allocator,
                                                                    F, STI);
}

namespace {

class AlphaStubObjectPass : public ModulePass {
public:
  static char ID;

  AlphaStubObjectPass(const AlphaTargetMachine &TM, raw_pwrite_stream &Out,
                      CodeGenFileType FileType)
      : ModulePass(ID), TM(TM), Out(Out), FileType(FileType) {}

  bool runOnModule(Module &M) override {
    switch (FileType) {
    case CodeGenFileType::AssemblyFile:
      emitAssembly(M);
      return false;
    case CodeGenFileType::ObjectFile:
      emitObject(M);
      return false;
    case CodeGenFileType::Null:
      return false;
    }
    return false;
  }

private:
  const AlphaTargetMachine &TM;
  raw_pwrite_stream &Out;
  CodeGenFileType FileType;
  mutable DenseMap<const Function *, uint64_t> FunctionOffsets;
  mutable uint64_t CurrentTextOffset = 0;

  SmallString<128> getSymbolName(const GlobalValue &GV) const {
    SmallString<128> Name;
    Mangler Mang;
    TM.getNameWithPrefix(Name, &GV, Mang);
    return Name;
  }

  bool shouldEmitGlobal(const GlobalValue &GV) const {
    return !GV.isDeclaration() && !GV.hasAvailableExternallyLinkage();
  }

  struct LoweredValue {
    enum KindTy { Unsupported, Register, Immediate } Kind = Unsupported;
    MCRegister Reg = 0;
    int64_t Imm = 0;

    static LoweredValue reg(MCRegister Reg) {
      LoweredValue V;
      V.Kind = Register;
      V.Reg = Reg;
      return V;
    }

    static LoweredValue imm(int64_t Imm) {
      LoweredValue V;
      V.Kind = Immediate;
      V.Imm = Imm;
      return V;
    }

    bool isSupported() const { return Kind != Unsupported; }
    bool isReg() const { return Kind == Register; }
    bool isImm() const { return Kind == Immediate; }
  };

  using StoreMap = DenseMap<const Value *, const Value *>;

  MCRegister getIntegerArgumentRegister(const Argument &Arg) const {
    if (!Arg.getType()->isIntegerTy() || Arg.getArgNo() >= 6)
      return 0;

    static const MCRegister ArgRegs[] = {Alpha::R16, Alpha::R17, Alpha::R18,
                                        Alpha::R19, Alpha::R20, Alpha::R21};
    return ArgRegs[Arg.getArgNo()];
  }

  MCRegister getGPRByNumber(uint64_t RegNo) const {
    static const MCRegister GPRs[] = {
        Alpha::R0,  Alpha::R1,  Alpha::R2,  Alpha::R3,
        Alpha::R4,  Alpha::R5,  Alpha::R6,  Alpha::R7,
        Alpha::R8,  Alpha::R9,  Alpha::R10, Alpha::R11,
        Alpha::R12, Alpha::R13, Alpha::R14, Alpha::R15,
        Alpha::R16, Alpha::R17, Alpha::R18, Alpha::R19,
        Alpha::R20, Alpha::R21, Alpha::R22, Alpha::R23,
        Alpha::R24, Alpha::R25, Alpha::R26, Alpha::R27,
        Alpha::R28, Alpha::R29, Alpha::R30, Alpha::R31};
    if (RegNo >= std::size(GPRs))
      return 0;
    return GPRs[RegNo];
  }

  StoreMap collectSimpleStores(const Function &F) const {
    StoreMap Stores;
    for (const BasicBlock &BB : F) {
      for (const Instruction &I : BB) {
        const auto *SI = dyn_cast<StoreInst>(&I);
        if (!SI)
          continue;

        const Value *Ptr = SI->getPointerOperand()->stripPointerCasts();
        auto Inserted = Stores.insert({Ptr, SI->getValueOperand()});
        if (!Inserted.second && Inserted.first->second != SI->getValueOperand())
          Inserted.first->second = nullptr;
      }
    }
    return Stores;
  }

  const Value *resolveStoredValue(const Value *V, const StoreMap &Stores) const {
    while (true) {
      if (const auto *LI = dyn_cast<LoadInst>(V)) {
        const Value *Ptr = LI->getPointerOperand()->stripPointerCasts();
        auto It = Stores.find(Ptr);
        if (It == Stores.end() || !It->second)
          return V;
        V = It->second;
        continue;
      }

      if (const auto *CI = dyn_cast<CastInst>(V)) {
        if (!CI->getType()->isIntegerTy())
          return V;
        V = CI->getOperand(0);
        continue;
      }

      return V;
    }
  }

  LoweredValue lowerSimpleValue(const Value *V, const StoreMap &Stores) const {
    V = resolveStoredValue(V, Stores);

    if (const auto *CI = dyn_cast<ConstantInt>(V)) {
      if (CI->getBitWidth() <= 64)
        return LoweredValue::imm(CI->getSExtValue());
      return LoweredValue();
    }

    if (const auto *Arg = dyn_cast<Argument>(V)) {
      if (MCRegister Reg = getIntegerArgumentRegister(*Arg))
        return LoweredValue::reg(Reg);
      return LoweredValue();
    }

    return LoweredValue();
  }

  const BinaryOperator *getReturnBinaryOperator(const Function &F,
                                                const StoreMap &Stores) const {
    const ReturnInst *Ret = getSingleReturnInst(F);
    if (!Ret || !Ret->getReturnValue())
      return nullptr;

    const Value *RV = resolveStoredValue(Ret->getReturnValue(), Stores);
    return dyn_cast<BinaryOperator>(RV);
  }

  const ReturnInst *getSingleReturnInst(const Function &F) const {
    const ReturnInst *Ret = nullptr;
    for (const BasicBlock &BB : F) {
      const Instruction *Term = BB.getTerminator();
      if (const auto *RI = dyn_cast<ReturnInst>(Term)) {
        if (Ret)
          return nullptr;
        Ret = RI;
        continue;
      }
      if (!isa<UnreachableInst>(Term))
        return nullptr;
    }
    return Ret;
  }

  std::optional<int64_t> getConstantReturnValue(const Function &F,
                                                const StoreMap &Stores) const {
    const ReturnInst *Ret = getSingleReturnInst(F);

    if (!Ret)
      return std::nullopt;

    Value *RV = Ret->getReturnValue();
    if (!RV)
      return 0;

    RV = const_cast<Value *>(resolveStoredValue(RV, Stores));
    const auto *CI = dyn_cast<ConstantInt>(RV);
    if (!CI || CI->getBitWidth() > 64)
      return std::nullopt;
    return CI->getSExtValue();
  }

  int64_t getEmittedReturnValue(const Function &F, const StoreMap &Stores) const {
    std::optional<int64_t> Ret = getConstantReturnValue(F, Stores);
    if (Ret && isInt<16>(*Ret))
      return *Ret;
    return 0;
  }

  void emitMCInst(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                  unsigned Opcode, ArrayRef<MCOperand> Operands) const {
    MCInst Inst;
    Inst.setOpcode(Opcode);
    for (const MCOperand &Operand : Operands)
      Inst.addOperand(Operand);
    Streamer.emitInstruction(Inst, STI);
    CurrentTextOffset += 4;
  }

  void emitLoadImm(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                   MCRegister Dest, int64_t Imm) const {
    emitMCInst(Streamer, STI, Alpha::LDA,
               {MCOperand::createReg(Dest), MCOperand::createImm(Imm),
                MCOperand::createReg(Alpha::R31)});
  }

  void emitCopyReg(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                   MCRegister Dest, MCRegister Src) const {
    if (Dest == Src)
      return;
    emitMCInst(Streamer, STI, Alpha::BISr,
               {MCOperand::createReg(Alpha::R31), MCOperand::createReg(Src),
                MCOperand::createReg(Dest)});
  }

  bool emitLoweredToReg(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                        LoweredValue V, MCRegister Dest) const {
    if (V.isReg()) {
      emitCopyReg(Streamer, STI, Dest, V.Reg);
      return true;
    }

    if (V.isImm() && isInt<16>(V.Imm)) {
      emitLoadImm(Streamer, STI, Dest, V.Imm);
      return true;
    }

    return false;
  }

  unsigned getLoweredToRegInstructionCount(LoweredValue V,
                                           MCRegister Dest) const {
    if (V.isReg())
      return V.Reg == Dest ? 0 : 1;
    if (V.isImm() && isInt<16>(V.Imm))
      return 1;
    return ~0U;
  }

  bool emitBinaryToReg(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                       const BinaryOperator *BO, const StoreMap &Stores,
                       MCRegister Dest) const {
    LoweredValue LHS = lowerSimpleValue(BO->getOperand(0), Stores);
    LoweredValue RHS = lowerSimpleValue(BO->getOperand(1), Stores);
    if (!LHS.isSupported() || !RHS.isSupported())
      return false;

    bool Is32Bit = BO->getType()->isIntegerTy() &&
                   BO->getType()->getIntegerBitWidth() <= 32;
    bool Commutative = BO->isCommutative();
    if (LHS.isImm() && RHS.isReg() && Commutative)
      std::swap(LHS, RHS);

    if (!LHS.isReg()) {
      if (!emitLoweredToReg(Streamer, STI, LHS, Alpha::R1))
        return false;
      LHS = LoweredValue::reg(Alpha::R1);
    }

    unsigned RegOpcode = 0;
    unsigned ImmOpcode = 0;
    switch (BO->getOpcode()) {
    case Instruction::Add:
      RegOpcode = Is32Bit ? Alpha::ADDLr : Alpha::ADDQr;
      ImmOpcode = Is32Bit ? Alpha::ADDLi : Alpha::ADDQi;
      break;
    case Instruction::Sub:
      RegOpcode = Is32Bit ? Alpha::SUBLr : Alpha::SUBQr;
      ImmOpcode = Is32Bit ? Alpha::SUBLi : Alpha::SUBQi;
      break;
    case Instruction::Mul:
      RegOpcode = Is32Bit ? Alpha::MULLr : Alpha::MULQr;
      ImmOpcode = Is32Bit ? Alpha::MULLi : Alpha::MULQi;
      break;
    case Instruction::And:
      RegOpcode = Alpha::ANDr;
      ImmOpcode = Alpha::ANDi;
      break;
    case Instruction::Or:
      RegOpcode = Alpha::BISr;
      ImmOpcode = Alpha::BISi;
      break;
    case Instruction::Xor:
      RegOpcode = Alpha::XORr;
      ImmOpcode = Alpha::XORi;
      break;
    default:
      return false;
    }

    if (RHS.isImm() && RHS.Imm >= 0 && RHS.Imm <= 255) {
      emitMCInst(Streamer, STI, ImmOpcode,
                 {MCOperand::createReg(LHS.Reg), MCOperand::createImm(RHS.Imm),
                  MCOperand::createReg(Dest)});
      return true;
    }

    if (!RHS.isReg()) {
      if (!emitLoweredToReg(Streamer, STI, RHS, Alpha::R1))
        return false;
      RHS = LoweredValue::reg(Alpha::R1);
    }

    emitMCInst(Streamer, STI, RegOpcode,
               {MCOperand::createReg(LHS.Reg), MCOperand::createReg(RHS.Reg),
                MCOperand::createReg(Dest)});
    return true;
  }

  bool emitValueToReg(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                      const Value *V, const StoreMap &Stores,
                      MCRegister Dest) const {
    V = resolveStoredValue(V, Stores);
    if (const auto *BO = dyn_cast<BinaryOperator>(V))
      return emitBinaryToReg(Streamer, STI, BO, Stores, Dest);

    return emitLoweredToReg(Streamer, STI, lowerSimpleValue(V, Stores), Dest);
  }

  unsigned getBinaryToRegInstructionCount(const BinaryOperator *BO,
                                          const StoreMap &Stores,
                                          MCRegister Dest) const {
    LoweredValue LHS = lowerSimpleValue(BO->getOperand(0), Stores);
    LoweredValue RHS = lowerSimpleValue(BO->getOperand(1), Stores);
    if (!LHS.isSupported() || !RHS.isSupported())
      return ~0U;

    if (LHS.isImm() && RHS.isReg() && BO->isCommutative())
      std::swap(LHS, RHS);

    unsigned Count = 1;
    if (!LHS.isReg()) {
      unsigned LoadCount = getLoweredToRegInstructionCount(LHS, Alpha::R1);
      if (LoadCount == ~0U)
        return ~0U;
      Count += LoadCount;
    }

    if (RHS.isImm() && RHS.Imm >= 0 && RHS.Imm <= 255)
      return Count;

    if (!RHS.isReg()) {
      unsigned LoadCount = getLoweredToRegInstructionCount(RHS, Alpha::R1);
      if (LoadCount == ~0U)
        return ~0U;
      Count += LoadCount;
    }
    return Count;
  }

  unsigned getValueToRegInstructionCount(const Value *V,
                                         const StoreMap &Stores,
                                         MCRegister Dest) const {
    V = resolveStoredValue(V, Stores);
    if (const auto *BO = dyn_cast<BinaryOperator>(V))
      return getBinaryToRegInstructionCount(BO, Stores, Dest);
    return getLoweredToRegInstructionCount(lowerSimpleValue(V, Stores), Dest);
  }

  unsigned getLoweredReturnInstructionCount(const Value *V,
                                            const StoreMap &Stores) const {
    return getValueToRegInstructionCount(V, Stores, Alpha::R0);
  }

  bool getSimpleReturnStore(const BasicBlock *BB, const Value *&Ptr,
                            const Value *&Stored,
                            const BasicBlock *&Successor) const {
    Ptr = nullptr;
    Stored = nullptr;
    Successor = nullptr;

    for (const Instruction &I : *BB) {
      if (const auto *SI = dyn_cast<StoreInst>(&I)) {
        if (Stored)
          return false;
        Ptr = SI->getPointerOperand()->stripPointerCasts();
        Stored = SI->getValueOperand();
        continue;
      }

      if (isa<AllocaInst>(I) || isa<LoadInst>(I))
        continue;

      if (!I.isTerminator() && !isa<DbgInfoIntrinsic>(I))
        return false;
    }

    const auto *Br = dyn_cast<UncondBrInst>(BB->getTerminator());
    if (!Br || !Stored)
      return false;
    Successor = Br->getSuccessor(0);
    return true;
  }

  bool mergeReturnsStoredPointer(const BasicBlock *Merge,
                                 const Value *Ptr) const {
    const auto *Ret = dyn_cast<ReturnInst>(Merge->getTerminator());
    if (!Ret || !Ret->getReturnValue())
      return false;

    const auto *LI = dyn_cast<LoadInst>(Ret->getReturnValue());
    if (!LI)
      return false;
    return LI->getPointerOperand()->stripPointerCasts() == Ptr;
  }

  bool emitBranchToFalse(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                         const Value *Cond, const StoreMap &Stores,
                         int64_t FalseDisp) const {
    Cond = resolveStoredValue(Cond, Stores);
    unsigned Opcode = Alpha::BEQ;
    const Value *BranchValue = Cond;

    if (const auto *ICI = dyn_cast<ICmpInst>(Cond)) {
      const Value *LHS = ICI->getOperand(0);
      const Value *RHS = ICI->getOperand(1);
      const auto *RC = dyn_cast<ConstantInt>(resolveStoredValue(RHS, Stores));
      if (RC && RC->isZero()) {
        switch (ICI->getPredicate()) {
        case ICmpInst::ICMP_NE:
          Opcode = Alpha::BEQ;
          break;
        case ICmpInst::ICMP_EQ:
          Opcode = Alpha::BNE;
          break;
        default:
          return false;
        }
        BranchValue = LHS;
      } else {
        LoweredValue LLV = lowerSimpleValue(LHS, Stores);
        LoweredValue RLV = lowerSimpleValue(RHS, Stores);
        if (!LLV.isReg() || !RLV.isSupported())
          return false;

        if (RLV.isReg()) {
          emitMCInst(Streamer, STI, Alpha::XORr,
                     {MCOperand::createReg(LLV.Reg),
                      MCOperand::createReg(RLV.Reg),
                      MCOperand::createReg(Alpha::R1)});
        } else if (RLV.isImm() && RLV.Imm >= 0 && RLV.Imm <= 255) {
          emitMCInst(Streamer, STI, Alpha::XORi,
                     {MCOperand::createReg(LLV.Reg),
                      MCOperand::createImm(RLV.Imm),
                      MCOperand::createReg(Alpha::R1)});
        } else {
          return false;
        }

        switch (ICI->getPredicate()) {
        case ICmpInst::ICMP_NE:
          Opcode = Alpha::BEQ;
          break;
        case ICmpInst::ICMP_EQ:
          Opcode = Alpha::BNE;
          break;
        default:
          return false;
        }
        emitMCInst(Streamer, STI, Opcode,
                   {MCOperand::createReg(Alpha::R1),
                    MCOperand::createImm(FalseDisp)});
        return true;
      }
    }

    LoweredValue LV = lowerSimpleValue(BranchValue, Stores);
    if (!LV.isReg())
      return false;

    emitMCInst(Streamer, STI, Opcode,
               {MCOperand::createReg(LV.Reg), MCOperand::createImm(FalseDisp)});
    return true;
  }

  void emitReturn(MCStreamer &Streamer, const MCSubtargetInfo &STI) const {
    emitMCInst(Streamer, STI, Alpha::RETDAG, {});
  }

  bool emitSimpleConditionalReturn(MCStreamer &Streamer,
                                   const MCSubtargetInfo &STI,
                                   const Function &F,
                                   const StoreMap &Stores) const {
    const auto *Br = dyn_cast<CondBrInst>(F.getEntryBlock().getTerminator());
    if (!Br)
      return false;

    const BasicBlock *TrueBB = Br->getSuccessor(0);
    const BasicBlock *FalseBB = Br->getSuccessor(1);
    const Value *TruePtr;
    const Value *TrueValue;
    const BasicBlock *TrueSucc;
    const Value *FalsePtr;
    const Value *FalseValue;
    const BasicBlock *FalseSucc;
    if (!getSimpleReturnStore(TrueBB, TruePtr, TrueValue, TrueSucc) ||
        !getSimpleReturnStore(FalseBB, FalsePtr, FalseValue, FalseSucc) ||
        TruePtr != FalsePtr || TrueSucc != FalseSucc ||
        !mergeReturnsStoredPointer(TrueSucc, TruePtr))
      return false;

    unsigned TrueCount = getLoweredReturnInstructionCount(TrueValue, Stores);
    unsigned FalseCount = getLoweredReturnInstructionCount(FalseValue, Stores);
    if (TrueCount == ~0U || FalseCount == ~0U)
      return false;

    if (!emitBranchToFalse(Streamer, STI, Br->getCondition(), Stores,
                           TrueCount + 1))
      return false;

    if (!emitValueToReg(Streamer, STI, TrueValue, Stores, Alpha::R0))
      return false;
    emitReturn(Streamer, STI);

    if (!emitValueToReg(Streamer, STI, FalseValue, Stores, Alpha::R0))
      return false;
    emitReturn(Streamer, STI);
    return true;
  }

  unsigned getBranchToFalseInstructionCount(const Value *Cond,
                                            const StoreMap &Stores) const {
    Cond = resolveStoredValue(Cond, Stores);
    if (const auto *ICI = dyn_cast<ICmpInst>(Cond)) {
      const Value *RHS = ICI->getOperand(1);
      const auto *RC = dyn_cast<ConstantInt>(resolveStoredValue(RHS, Stores));
      if (RC && RC->isZero())
        return 1;

      LoweredValue LHS = lowerSimpleValue(ICI->getOperand(0), Stores);
      LoweredValue RLV = lowerSimpleValue(RHS, Stores);
      if (!LHS.isReg() || !RLV.isSupported())
        return ~0U;
      if (RLV.isReg() || (RLV.isImm() && RLV.Imm >= 0 && RLV.Imm <= 255))
        return 2;
      return ~0U;
    }

    return lowerSimpleValue(Cond, Stores).isReg() ? 1 : ~0U;
  }

  unsigned getSimpleConditionalReturnInstructionCount(
      const Function &F, const StoreMap &Stores) const {
    const auto *Br = dyn_cast<CondBrInst>(F.getEntryBlock().getTerminator());
    if (!Br)
      return ~0U;

    const BasicBlock *TrueBB = Br->getSuccessor(0);
    const BasicBlock *FalseBB = Br->getSuccessor(1);
    const Value *TruePtr;
    const Value *TrueValue;
    const BasicBlock *TrueSucc;
    const Value *FalsePtr;
    const Value *FalseValue;
    const BasicBlock *FalseSucc;
    if (!getSimpleReturnStore(TrueBB, TruePtr, TrueValue, TrueSucc) ||
        !getSimpleReturnStore(FalseBB, FalsePtr, FalseValue, FalseSucc) ||
        TruePtr != FalsePtr || TrueSucc != FalseSucc ||
        !mergeReturnsStoredPointer(TrueSucc, TruePtr))
      return ~0U;

    unsigned CondCount = getBranchToFalseInstructionCount(Br->getCondition(),
                                                          Stores);
    unsigned TrueCount = getLoweredReturnInstructionCount(TrueValue, Stores);
    unsigned FalseCount = getLoweredReturnInstructionCount(FalseValue, Stores);
    if (CondCount == ~0U || TrueCount == ~0U || FalseCount == ~0U)
      return ~0U;
    return CondCount + TrueCount + 1 + FalseCount + 1;
  }

  std::optional<std::string> expandInlineAsm(const InlineAsm &IA,
                                             const CallBase &CB) const {
    StringRef Asm = IA.getAsmString();
    std::string Expanded;
    for (size_t I = 0, E = Asm.size(); I != E;) {
      if (Asm[I] != '$') {
        Expanded.push_back(Asm[I++]);
        continue;
      }

      if (I + 1 != E && Asm[I + 1] == '$') {
        Expanded.push_back('$');
        I += 2;
        continue;
      }

      size_t NumStart = I + 1;
      size_t NumEnd = NumStart;
      while (NumEnd != E && llvm::isDigit(Asm[NumEnd]))
        ++NumEnd;
      if (NumStart == NumEnd) {
        Expanded.push_back(Asm[I++]);
        continue;
      }

      unsigned OperandNo;
      if (Asm.slice(NumStart, NumEnd).getAsInteger(10, OperandNo) ||
          OperandNo >= CB.arg_size())
        return std::nullopt;

      const auto *CI = dyn_cast<ConstantInt>(CB.getArgOperand(OperandNo));
      if (!CI || CI->getBitWidth() > 64)
        return std::nullopt;
      Expanded += Twine(CI->getSExtValue()).str();
      I = NumEnd;
    }
    return Expanded;
  }

  bool collectNakedInlineAsmStatements(
      const Function &F, SmallVectorImpl<std::string> &Statements) const {
    if (!F.hasFnAttribute(Attribute::Naked))
      return false;

    for (const BasicBlock &BB : F) {
      for (const Instruction &I : BB) {
        const auto *CB = dyn_cast<CallBase>(&I);
        if (!CB)
          continue;

        const auto *IA = dyn_cast<InlineAsm>(CB->getCalledOperand());
        if (!IA)
          continue;

        std::optional<std::string> Expanded = expandInlineAsm(*IA, *CB);
        if (!Expanded)
          return false;

        SmallVector<StringRef, 8> Pieces;
        StringRef(*Expanded).split(Pieces, ';');
        for (StringRef Piece : Pieces) {
          Piece = Piece.trim();
          if (!Piece.empty())
            Statements.push_back(Piece.str());
        }
      }
    }

    return !Statements.empty();
  }

  std::optional<MCRegister> parseAsmRegister(StringRef &Text) const {
    Text = Text.ltrim();
    if (!Text.consume_front("$"))
      return std::nullopt;

    size_t Size = 0;
    while (Size != Text.size() && llvm::isDigit(Text[Size]))
      ++Size;
    if (!Size)
      return std::nullopt;

    uint64_t RegNo;
    if (Text.take_front(Size).getAsInteger(10, RegNo))
      return std::nullopt;
    Text = Text.drop_front(Size);

    MCRegister Reg = getGPRByNumber(RegNo);
    if (!Reg)
      return std::nullopt;
    return Reg;
  }

  std::optional<int64_t> parseAsmInteger(StringRef &Text,
                                         StringRef Terminators) const {
    Text = Text.ltrim();
    size_t Size = 0;
    while (Size != Text.size() && !llvm::is_contained(Terminators, Text[Size]))
      ++Size;
    StringRef Number = Text.take_front(Size).trim();
    if (Number.empty())
      return std::nullopt;
    int64_t Value;
    if (Number.getAsInteger(0, Value))
      return std::nullopt;
    Text = Text.drop_front(Size);
    return Value;
  }

  bool emitInlineAsmStatement(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                              StringRef Statement) const {
    Statement = Statement.trim();
    if (Statement == "ret") {
      emitMCInst(Streamer, STI, Alpha::RETDAG, {});
      return true;
    }

    if (Statement.consume_front("call_pal")) {
      std::optional<int64_t> Function = parseAsmInteger(Statement, "");
      if (!Function || !Statement.trim().empty())
        return false;
      emitMCInst(Streamer, STI, Alpha::CALL_PAL,
                 {MCOperand::createImm(*Function)});
      return true;
    }

    if (Statement.consume_front("lda")) {
      std::optional<MCRegister> Ra = parseAsmRegister(Statement);
      Statement = Statement.ltrim();
      if (!Ra || !Statement.consume_front(","))
        return false;
      std::optional<int64_t> Disp = parseAsmInteger(Statement, "(");
      if (!Disp || !Statement.consume_front("("))
        return false;
      std::optional<MCRegister> Rb = parseAsmRegister(Statement);
      if (!Rb || !Statement.consume_front(")") || !Statement.trim().empty())
        return false;
      emitMCInst(Streamer, STI, Alpha::LDA,
                 {MCOperand::createReg(*Ra), MCOperand::createImm(*Disp),
                  MCOperand::createReg(*Rb)});
      return true;
    }

    return false;
  }

  bool emitNakedInlineAsmObject(MCStreamer &Streamer,
                                const MCSubtargetInfo &STI,
                                const Function &F) const {
    SmallVector<std::string, 8> Statements;
    if (!collectNakedInlineAsmStatements(F, Statements))
      return false;

    for (StringRef Statement : Statements)
      if (!emitInlineAsmStatement(Streamer, STI, Statement))
        return false;
    return true;
  }

  bool emitCallToNakedInlineAsm(MCStreamer &Streamer,
                                const MCSubtargetInfo &STI, const CallBase &CB,
                                const StoreMap &Stores) const {
    const auto *Callee = dyn_cast_or_null<Function>(CB.getCalledOperand());
    if (!Callee || !Callee->hasFnAttribute(Attribute::Naked) ||
        CB.arg_size() > 6)
      return false;

    static const MCRegister ArgRegs[] = {Alpha::R16, Alpha::R17, Alpha::R18,
                                        Alpha::R19, Alpha::R20, Alpha::R21};
    for (unsigned I = 0, E = CB.arg_size(); I != E; ++I)
      if (!emitValueToReg(Streamer, STI, CB.getArgOperand(I), Stores,
                          ArgRegs[I]))
        return false;

    SmallVector<std::string, 8> Statements;
    if (!collectNakedInlineAsmStatements(*Callee, Statements))
      return false;

    for (StringRef Statement : Statements)
      if (!emitInlineAsmStatement(Streamer, STI, Statement))
        return false;
    return true;
  }

  unsigned getCallToFunctionInstructionCount(const CallBase &CB,
                                             const StoreMap &Stores) const {
    const auto *Callee = dyn_cast_or_null<Function>(CB.getCalledOperand());
    if (!Callee || Callee->isIntrinsic() || CB.arg_size() > 6)
      return ~0U;

    static const MCRegister ArgRegs[] = {Alpha::R16, Alpha::R17, Alpha::R18,
                                        Alpha::R19, Alpha::R20, Alpha::R21};
    unsigned Count = 2; // Save return address.
    for (unsigned I = 0, E = CB.arg_size(); I != E; ++I) {
      unsigned ArgCount = getValueToRegInstructionCount(CB.getArgOperand(I),
                                                        Stores, ArgRegs[I]);
      if (ArgCount == ~0U)
        return ~0U;
      Count += ArgCount;
    }
    return Count + 4; // bsr, restore return address, deallocate, ret.
  }

  bool emitCallToFunction(MCStreamer &Streamer, const MCSubtargetInfo &STI,
                          const CallBase &CB, const StoreMap &Stores) const {
    const auto *Callee = dyn_cast_or_null<Function>(CB.getCalledOperand());
    if (!Callee || Callee->isIntrinsic() || CB.arg_size() > 6)
      return false;

    emitMCInst(Streamer, STI, Alpha::LDA,
               {MCOperand::createReg(Alpha::R30), MCOperand::createImm(-16),
                MCOperand::createReg(Alpha::R30)});
    emitMCInst(Streamer, STI, Alpha::STQ,
               {MCOperand::createReg(Alpha::R26), MCOperand::createImm(0),
                MCOperand::createReg(Alpha::R30)});

    static const MCRegister ArgRegs[] = {Alpha::R16, Alpha::R17, Alpha::R18,
                                        Alpha::R19, Alpha::R20, Alpha::R21};
    for (unsigned I = 0, E = CB.arg_size(); I != E; ++I)
      if (!emitValueToReg(Streamer, STI, CB.getArgOperand(I), Stores,
                          ArgRegs[I]))
        return false;

    auto TargetIt = FunctionOffsets.find(Callee);
    if (TargetIt != FunctionOffsets.end()) {
      int64_t Disp = (static_cast<int64_t>(TargetIt->second) -
                      static_cast<int64_t>(CurrentTextOffset + 4)) /
                     4;
      emitMCInst(Streamer, STI, Alpha::BSR, {MCOperand::createImm(Disp)});
    } else {
      SmallString<128> Name = getSymbolName(*Callee);
      MCSymbol *Sym = Streamer.getContext().getOrCreateSymbol(Name);
      const MCExpr *Expr = MCSymbolRefExpr::create(Sym, Streamer.getContext());
      emitMCInst(Streamer, STI, Alpha::BSR, {MCOperand::createExpr(Expr)});
    }

    emitMCInst(Streamer, STI, Alpha::LDQ,
               {MCOperand::createReg(Alpha::R26), MCOperand::createImm(0),
                MCOperand::createReg(Alpha::R30)});
    emitMCInst(Streamer, STI, Alpha::LDA,
               {MCOperand::createReg(Alpha::R30), MCOperand::createImm(16),
                MCOperand::createReg(Alpha::R30)});
    emitReturn(Streamer, STI);
    return true;
  }

  unsigned getFunctionInstructionCount(const Function &F) const {
    SmallVector<std::string, 8> Statements;
    if (collectNakedInlineAsmStatements(F, Statements))
      return Statements.size();

    StoreMap Stores = collectSimpleStores(F);
    unsigned ConditionalCount = getSimpleConditionalReturnInstructionCount(F,
                                                                           Stores);
    if (ConditionalCount != ~0U)
      return ConditionalCount;

    const ReturnInst *RetInst = getSingleReturnInst(F);
    if (RetInst && RetInst->getReturnValue()) {
      const Value *RV = resolveStoredValue(RetInst->getReturnValue(), Stores);
      if (const auto *CB = dyn_cast<CallBase>(RV)) {
        unsigned CallCount = getCallToFunctionInstructionCount(*CB, Stores);
        if (CallCount != ~0U)
          return CallCount;
      }

      unsigned ValueCount = getValueToRegInstructionCount(RetInst->getReturnValue(),
                                                          Stores, Alpha::R0);
      if (ValueCount != ~0U)
        return ValueCount + 1;
    }

    return 2;
  }

  void emitFunctionBodyAssembly(const Function &F) {
    SmallVector<std::string, 8> Statements;
    if (collectNakedInlineAsmStatements(F, Statements)) {
      for (StringRef Statement : Statements)
        Out << "\t" << Statement << "\n";
      return;
    }

    StoreMap Stores = collectSimpleStores(F);
    int64_t Ret = getEmittedReturnValue(F, Stores);
    Out << "\tlda\t$0," << Ret << "($31)\n";
    Out << "\tret\n";
  }

  void emitFunctionBodyObject(MCStreamer &Streamer,
                              const MCSubtargetInfo &STI,
                              const Function &F) {
    if (emitNakedInlineAsmObject(Streamer, STI, F))
      return;

    StoreMap Stores = collectSimpleStores(F);
    if (emitSimpleConditionalReturn(Streamer, STI, F, Stores))
      return;

    const ReturnInst *RetInst = getSingleReturnInst(F);
    bool EmittedReturn = false;
    if (RetInst && RetInst->getReturnValue()) {
      const Value *RV = resolveStoredValue(RetInst->getReturnValue(), Stores);
      if (const auto *CB = dyn_cast<CallBase>(RV))
        if (emitCallToFunction(Streamer, STI, *CB, Stores))
          return;
      if (!EmittedReturn)
        EmittedReturn = emitValueToReg(Streamer, STI, RetInst->getReturnValue(),
                                       Stores, Alpha::R0);
    }

    if (!EmittedReturn)
      emitLoadImm(Streamer, STI, Alpha::R0, getEmittedReturnValue(F, Stores));

    emitReturn(Streamer, STI);
  }

  void emitAssembly(Module &M) {
    for (const Function &F : M.functions()) {
      if (!shouldEmitGlobal(F))
        continue;
      SmallString<128> Name = getSymbolName(F);
      Out << "\t.text\n";
      if (!F.hasLocalLinkage())
        Out << "\t.globl\t" << Name << "\n";
      Out << Name << ":\n";
      emitFunctionBodyAssembly(F);
    }
  }

  void emitGlobalSymbol(MCStreamer &Streamer, MCContext &Ctx,
                        const DataLayout &DL, const GlobalValue &GV,
                        MCSection *Section) {
    SmallString<128> Name = getSymbolName(GV);
    MCSymbol *Sym = Ctx.getOrCreateSymbol(Name);
    Streamer.switchSection(Section);
    Align Alignment = GV.getPointerAlignment(DL);
    if (Alignment > Align(1))
      Streamer.emitValueToAlignment(Alignment);
    if (!GV.hasLocalLinkage() && !GV.isWeakForLinker())
      Streamer.emitSymbolAttribute(Sym, MCSA_Global);
    Streamer.emitLabel(Sym);
  }

  bool emitCommonGlobal(MCStreamer &Streamer, MCContext &Ctx,
                        const DataLayout &DL, const GlobalVariable &GV) {
    if (!GV.hasCommonLinkage())
      return false;

    SmallString<128> Name = getSymbolName(GV);
    MCSymbol *Sym = Ctx.getOrCreateSymbol(Name);
    uint64_t Size = GV.getGlobalSize(DL);
    Streamer.emitCommonSymbol(Sym, Size ? Size : 1, GV.getPointerAlignment(DL));
    return true;
  }

  bool emitGlobalInitializer(MCStreamer &Streamer, MCContext &Ctx,
                             const DataLayout &DL, const Constant *C) {
    if (isa<ConstantAggregateZero>(C) || isa<UndefValue>(C) ||
        isa<ConstantPointerNull>(C)) {
      Streamer.emitZeros(DL.getTypeAllocSize(C->getType()));
      return true;
    }

    if (const auto *CI = dyn_cast<ConstantInt>(C)) {
      Streamer.emitIntValue(CI->getZExtValue(), DL.getTypeStoreSize(CI->getType()));
      return true;
    }

    if (const auto *CFP = dyn_cast<ConstantFP>(C)) {
      Streamer.emitIntValue(CFP->getValueAPF().bitcastToAPInt());
      return true;
    }

    if (const auto *CDS = dyn_cast<ConstantDataSequential>(C)) {
      Streamer.emitBytes(CDS->getRawDataValues());
      uint64_t AllocSize = DL.getTypeAllocSize(CDS->getType());
      uint64_t Emitted = CDS->getRawDataValues().size();
      if (AllocSize > Emitted)
        Streamer.emitZeros(AllocSize - Emitted);
      return true;
    }

    if (const auto *GV = dyn_cast<GlobalValue>(C->stripPointerCasts())) {
      SmallString<128> Name = getSymbolName(*GV);
      MCSymbol *Sym = Ctx.getOrCreateSymbol(Name);
      Streamer.emitValue(MCSymbolRefExpr::create(Sym, Ctx),
                         DL.getPointerSize());
      return true;
    }

    if (const auto *CA = dyn_cast<ConstantArray>(C)) {
      for (const Use &Op : CA->operands())
        if (!emitGlobalInitializer(Streamer, Ctx, DL, cast<Constant>(Op.get())))
          return false;
      return true;
    }

    if (const auto *CS = dyn_cast<ConstantStruct>(C)) {
      const StructLayout *SL = DL.getStructLayout(CS->getType());
      uint64_t Offset = 0;
      for (unsigned I = 0, E = CS->getNumOperands(); I != E; ++I) {
        uint64_t FieldOffset = SL->getElementOffset(I);
        if (FieldOffset > Offset)
          Streamer.emitZeros(FieldOffset - Offset);
        const auto *Field = cast<Constant>(CS->getOperand(I));
        if (!emitGlobalInitializer(Streamer, Ctx, DL, Field))
          return false;
        Offset = FieldOffset + DL.getTypeAllocSize(Field->getType());
      }
      if (SL->getSizeInBytes() > Offset)
        Streamer.emitZeros(SL->getSizeInBytes() - Offset);
      return true;
    }

    if (const auto *CE = dyn_cast<ConstantExpr>(C)) {
      if (CE->isCast())
        return emitGlobalInitializer(Streamer, Ctx, DL, CE->getOperand(0));
    }

    Ctx.reportError(SMLoc(), "unsupported Alpha global initializer");
    return false;
  }

  MCSymbol *beginCVSubsection(MCStreamer &Streamer, MCContext &Ctx,
                              DebugSubsectionKind Kind) {
    MCSymbol *Begin = Ctx.createTempSymbol();
    MCSymbol *End = Ctx.createTempSymbol();
    Streamer.emitInt32(static_cast<uint32_t>(Kind));
    Streamer.emitAbsoluteSymbolDiff(End, Begin, 4);
    Streamer.emitLabel(Begin);
    return End;
  }

  void endCVSubsection(MCStreamer &Streamer, MCSymbol *End) {
    Streamer.emitLabel(End);
    Streamer.emitValueToAlignment(Align(4));
  }

  MCSymbol *beginCVSymbolRecord(MCStreamer &Streamer, MCContext &Ctx,
                                SymbolKind Kind) {
    MCSymbol *Begin = Ctx.createTempSymbol();
    MCSymbol *End = Ctx.createTempSymbol();
    Streamer.emitAbsoluteSymbolDiff(End, Begin, 2);
    Streamer.emitLabel(Begin);
    Streamer.emitInt16(static_cast<uint16_t>(Kind));
    return End;
  }

  void endCVSymbolRecord(MCStreamer &Streamer, MCSymbol *End) {
    Streamer.emitValueToAlignment(Align(4));
    Streamer.emitLabel(End);
  }

  void emitCVString(MCStreamer &Streamer, StringRef S) {
    SmallString<256> Str(S.take_front(0xf000 - 1));
    Str.push_back('\0');
    Streamer.emitBytes(Str);
  }

  SourceLanguage getCVSourceLanguage(const DICompileUnit *CU) {
    if (!CU)
      return SourceLanguage::C;

    DISourceLanguageName Lang = CU->getSourceLanguage();
    dwarf::SourceLanguageName Name =
        Lang.hasVersionedName()
            ? static_cast<dwarf::SourceLanguageName>(Lang.getName())
            : dwarf::toDW_LNAME(static_cast<dwarf::SourceLanguage>(
                                    Lang.getName()))
                  .value_or(std::make_pair(dwarf::DW_LNAME_C, 0))
                  .first;
    switch (Name) {
    case dwarf::DW_LNAME_C_plus_plus:
      return SourceLanguage::Cpp;
    case dwarf::DW_LNAME_C:
    default:
      return SourceLanguage::C;
    }
  }

  void emitCodeViewCompilerInfo(MCStreamer &Streamer, MCContext &Ctx,
                                MCObjectFileInfo &MOFI, Module &M) {
    if (!TM.getTargetTriple().isOSBinFormatCOFF() ||
        M.getModuleFlag("SkipCodeViewEmission"))
      return;
    MCSection *DebugS = MOFI.getCOFFDebugSymbolsSection();
    if (!DebugS)
      return;

    NamedMDNode *CUs = M.getNamedMetadata("llvm.dbg.cu");
    if (!CUs || CUs->operands().empty())
      return;
    const auto *CU = cast<DICompileUnit>(*CUs->operands().begin());

    Streamer.switchSection(DebugS);
    Streamer.emitValueToAlignment(Align(4));
    Streamer.emitInt32(COFF::DEBUG_SECTION_MAGIC);

    MCSymbol *SymbolsEnd =
        beginCVSubsection(Streamer, Ctx, DebugSubsectionKind::Symbols);

    MCSymbol *ObjNameEnd =
        beginCVSymbolRecord(Streamer, Ctx, SymbolKind::S_OBJNAME);
    Streamer.emitInt32(0);
    StringRef ObjName = TM.Options.ObjectFilenameForDebug;
    emitCVString(Streamer, (ObjName == "-" ? StringRef() : ObjName));
    endCVSymbolRecord(Streamer, ObjNameEnd);

    MCSymbol *CompileEnd =
        beginCVSymbolRecord(Streamer, Ctx, SymbolKind::S_COMPILE3);
    Streamer.emitInt32(static_cast<uint8_t>(getCVSourceLanguage(CU)));
    Streamer.emitInt16(static_cast<uint16_t>(CPUType::Alpha));
    for (int I = 0; I != 4; ++I)
      Streamer.emitInt16(0);
    Streamer.emitInt16(std::min<int>(1000 * LLVM_VERSION_MAJOR +
                                         10 * LLVM_VERSION_MINOR +
                                         LLVM_VERSION_PATCH,
                                     std::numeric_limits<uint16_t>::max()));
    for (int I = 0; I != 3; ++I)
      Streamer.emitInt16(0);
    emitCVString(Streamer, CU->getProducer());
    endCVSymbolRecord(Streamer, CompileEnd);

    endCVSubsection(Streamer, SymbolsEnd);
  }

  void emitObject(Module &M) {
    const Triple &TT = TM.getTargetTriple();
    const MCAsmInfo &MAI = TM.getMCAsmInfo();
    const MCRegisterInfo &MRI = TM.getMCRegisterInfo();
    const MCSubtargetInfo &STI = TM.getMCSubtargetInfo();
    MCContext Ctx(TT, MAI, MRI, STI, nullptr);
    std::unique_ptr<MCObjectFileInfo> MOFI(
        TM.getTarget().createMCObjectFileInfo(Ctx, TM.isPositionIndependent(),
                                              /*LargeCodeModel=*/false));
    Ctx.setObjectFileInfo(MOFI.get());

    std::unique_ptr<MCAsmBackend> MAB(
        TM.getTarget().createMCAsmBackend(STI, MRI, TM.Options.MCOptions));
    std::unique_ptr<MCObjectWriter> OW = MAB->createObjectWriter(Out);
    std::unique_ptr<MCCodeEmitter> CE(
        TM.getTarget().createMCCodeEmitter(*TM.getMCInstrInfo(), Ctx));
    std::unique_ptr<MCStreamer> Streamer(TM.getTarget().createMCObjectStreamer(
        TT, Ctx, std::move(MAB), std::move(OW), std::move(CE), STI));
    Streamer->initSections(STI);
    const DataLayout &DL = M.getDataLayout();

    FunctionOffsets.clear();
    uint64_t TextOffset = 0;
    for (const Function &F : M.functions()) {
      if (!shouldEmitGlobal(F))
        continue;
      FunctionOffsets[&F] = TextOffset;
      TextOffset += static_cast<uint64_t>(getFunctionInstructionCount(F)) * 4;
    }

    MCSection *Text = MOFI->getTextSection();
    for (const Function &F : M.functions()) {
      if (!shouldEmitGlobal(F))
        continue;
      CurrentTextOffset = FunctionOffsets[&F];
      emitGlobalSymbol(*Streamer, Ctx, DL, F, Text);
      emitFunctionBodyObject(*Streamer, STI, F);
    }

    MCSection *Data = MOFI->getDataSection();
    for (const GlobalVariable &GV : M.globals()) {
      if (!shouldEmitGlobal(GV))
        continue;
      if (emitCommonGlobal(*Streamer, Ctx, DL, GV))
        continue;
      emitGlobalSymbol(*Streamer, Ctx, DL, GV, Data);
      if (GV.hasInitializer()) {
        if (!emitGlobalInitializer(*Streamer, Ctx, DL, GV.getInitializer())) {
          Type *ValueTy = GV.getValueType();
          uint64_t Size = DL.getTypeAllocSize(ValueTy);
          Streamer->emitZeros(Size ? Size : 1);
        }
      } else {
        Type *ValueTy = GV.getValueType();
        uint64_t Size = DL.getTypeAllocSize(ValueTy);
        Streamer->emitZeros(Size ? Size : 1);
      }
    }

    emitCodeViewCompilerInfo(*Streamer, Ctx, *MOFI, M);

    Streamer->finish();
  }
};

char AlphaStubObjectPass::ID = 0;

} // end anonymous namespace
