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
#include "AlphaMCTargetDesc.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
using namespace llvm;

AlphaMCAsmInfo::AlphaMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  AlignmentIsInBytes = false;
  WeakRefDirective = "\t.weak\t";
  if (TT.isOSBinFormatELF()) {
    CodePointerSize = 8;
    CalleeSaveStackSlotSize = 8;
    SupportsDebugInformation = true;
    ExceptionsType = ExceptionHandling::DwarfCFI;
  }
}

static StringRef getAlphaSpecifierName(uint16_t Specifier) {
  switch (static_cast<Alpha::Specifier>(Specifier)) {
  case Alpha::S_LITERAL:
    return "literal";
  case Alpha::S_LITUSE_ADDR:
    return "lituse_addr";
  case Alpha::S_LITUSE_BASE:
    return "lituse_base";
  case Alpha::S_LITUSE_BYTOFF:
    return "lituse_bytoff";
  case Alpha::S_LITUSE_JSR:
    return "lituse_jsr";
  case Alpha::S_LITUSE_TLSGD:
    return "lituse_tlsgd";
  case Alpha::S_LITUSE_TLSLDM:
    return "lituse_tlsldm";
  case Alpha::S_LITUSE_JSRDIRECT:
    return "lituse_jsrdirect";
  case Alpha::S_GPDISP:
    return "gpdisp";
  case Alpha::S_GPRELHIGH:
    return "gprelhigh";
  case Alpha::S_GPRELLOW:
    return "gprellow";
  case Alpha::S_GPREL16:
    return "gprel";
  case Alpha::S_GPREL32:
    return "gprel32";
  case Alpha::S_BRSGP:
    return "samegp";
  case Alpha::S_TLSGD:
    return "tlsgd";
  case Alpha::S_TLSLDM:
    return "tlsldm";
  case Alpha::S_GOTDTPREL:
    return "gotdtprel";
  case Alpha::S_DTPRELHI:
    return "dtprelhi";
  case Alpha::S_DTPRELLO:
    return "dtprello";
  case Alpha::S_DTPREL16:
    return "dtprel";
  case Alpha::S_GOTTPREL:
    return "gottprel";
  case Alpha::S_TPRELHI:
    return "tprelhi";
  case Alpha::S_TPRELLO:
    return "tprello";
  case Alpha::S_TPREL16:
    return "tprel";
  case Alpha::S_None:
    break;
  }
  llvm_unreachable("invalid Alpha relocation specifier");
}

void AlphaMCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                        const MCSpecifierExpr &Expr) const {
  printExpr(OS, *Expr.getSubExpr());
  OS << '!' << getAlphaSpecifierName(Expr.getSpecifier());
}

bool AlphaMCAsmInfo::evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr,
                                               MCValue &Res,
                                               const MCAssembler *Asm) const {
  if (!Expr.getSubExpr()->evaluateAsRelocatable(Res, Asm))
    return false;
  Res.setSpecifier(Expr.getSpecifier());
  return !Res.getSubSym();
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
