//===-- AlphaMCAsmInfo.cpp - Alpha asm properties ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the AlphaMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "AlphaMCAsmInfo.h"
using namespace llvm;

AlphaMCAsmInfo::AlphaMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfo(Options) {
  AlignmentIsInBytes = false;
  WeakRefDirective = "\t.weak\t";
}

AlphaMCAsmInfoMicrosoftCOFF::AlphaMCAsmInfoMicrosoftCOFF(
    const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoMicrosoft(Options) {
  AlignmentIsInBytes = false;
  InternalSymbolPrefix = ".L";
  WeakRefDirective = "\t.weak\t";
  ExceptionsType = ExceptionHandling::WinEH;
  WinEHEncodingType = WinEH::EncodingType::Alpha;
}
