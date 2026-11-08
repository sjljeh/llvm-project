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

// The data layout is computed from the triple and the ABI name. Windows TASO
// uses a 32-bit address space, so select it through the "taso" ABI name when
// the +taso feature is requested without an explicit ABI.
static TargetOptions getAlphaTargetOptions(const Triple &TT, StringRef FS,
                                           TargetOptions Options) {
  if (TT.isOSWindows() && hasTASOFeature(FS) &&
      Options.MCOptions.ABIName.empty())
    Options.MCOptions.ABIName = "taso";
  return Options;
}

AlphaTargetMachine::AlphaTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : CodeGenTargetMachineImpl(T, TT, CPU, FS,
                               getAlphaTargetOptions(TT, FS, Options),
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

  bool addPreISel() override {
    if (TM->getTargetTriple().isOSWindows())
      addPass(createAlphaWinEHStatePass());
    return false;
  }

  bool addInstSelector() override {
    addPass(createAlphaISelDag(getAlphaTargetMachine()));
    return false;
  }

  void addMachineLateOptimization() override {
    addPass(&MachineLateInstrsCleanupID);
    if (!getAlphaTargetMachine().requiresStructuredCFG())
      addPass(&TailDuplicateLegacyID);
    addPass(&MachineCopyPropagationID);
  }

  void addPreEmitPass2() override {
    // Lower the COND_BRANCH pseudos at every optimization level; late
    // optimization does not run at -O0. Both passes depend on the final block
    // order, so they run after FuncletLayout: alignment nops after a funclet
    // return would otherwise hide it from EH scope analysis.
    addPass(createAlphaBranchSelectionPass());
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
