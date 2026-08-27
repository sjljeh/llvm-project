//===- AlphaSubtarget.cpp - Alpha Subtarget Information ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Alpha specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "AlphaSubtarget.h"
#include "Alpha.h"
#include "AlphaTargetMachine.h"
#include "llvm/CodeGen/LibcallLoweringInfo.h"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "AlphaGenSubtargetInfo.inc"

using namespace llvm;

AlphaSubtarget::AlphaSubtarget(const Triple &TT, StringRef CPU,
                               StringRef FS, AlphaTargetMachine &TM)
  : AlphaGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS), HasCT(false),
    IsTASO(false),
    InstrInfo(*this), FrameLowering(*this), TLInfo(TM, *this), TSInfo() {
  std::string CPUName = std::string(CPU);
  if (CPUName.empty())
    CPUName = "generic";

  // Parse features string.
  ParseSubtargetFeatures(CPUName, CPUName, FS);

  // Initialize scheduling itinerary for the specified CPU.
  InstrItins = getInstrItineraryForCPU(CPUName);
}

void AlphaSubtarget::initLibcallLoweringInfo(
    LibcallLoweringInfo &Info) const {
  Info.setLibcallImpl(RTLIB::MEMCPY, RTLIB::impl_memcpy);
  Info.setLibcallImpl(RTLIB::MEMMOVE, RTLIB::impl_memmove);
  Info.setLibcallImpl(RTLIB::MEMSET, RTLIB::impl_memset);
}
