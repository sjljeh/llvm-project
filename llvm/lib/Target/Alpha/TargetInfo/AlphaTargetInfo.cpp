//===-- AlphaTargetInfo.cpp - Alpha Target Implementation -----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "AlphaTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
using namespace llvm;

Target &llvm::getTheAlphaTarget() {
  static Target TheAlphaTarget;
  return TheAlphaTarget;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAlphaTargetInfo() {
  RegisterTarget<Triple::alpha, /*HasJIT=*/false> X(
      getTheAlphaTarget(), "alpha", "Alpha [experimental]", "Alpha");
}
