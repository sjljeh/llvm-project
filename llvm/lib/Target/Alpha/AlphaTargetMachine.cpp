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

// The data layout is derived from the triple and the ABI name, so select the
// TASO ABI when the truncated address space feature is requested.
static TargetOptions getEffectiveOptions(const Triple &TT, StringRef FS,
                                         TargetOptions Options) {
  if (TT.isOSWindows() && hasTASOFeature(FS))
    Options.MCOptions.ABIName = "taso";
  return Options;
}

AlphaTargetMachine::AlphaTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT, CPU, FS,
                               getEffectiveOptions(TT, FS, Options),
                               getEffectiveRelocModel(RM),
                               getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(createTLOF(TT)) {
  initAsmInfo();
}

AlphaTargetMachine::~AlphaTargetMachine() = default;

const AlphaSubtarget *
AlphaTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");
  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  std::string Key = CPU;
  Key.push_back('\0');
  Key += FS;
  auto &I = SubtargetMap[Key];
  if (!I)
    I = std::make_unique<AlphaSubtarget>(TargetTriple, CPU, FS, *this);
  return I.get();
}

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
    addPass(createAlphaLLRPPass());
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
