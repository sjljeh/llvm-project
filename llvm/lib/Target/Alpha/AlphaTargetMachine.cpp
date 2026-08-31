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
#include "TargetInfo/AlphaTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

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
