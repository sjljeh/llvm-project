//===-- RISCVMCAsmInfo.cpp - RISC-V Asm properties ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declarations of the RISCVMCAsmInfo properties.
//
//===----------------------------------------------------------------------===//

#include "RISCVMCAsmInfo.h"
#include "llvm/ADT/Enum.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCValue.h"
#include "llvm/TargetParser/Triple.h"
using namespace llvm;

constexpr EnumStringDef<MCAsmInfo::AtSpecifierKind> COFFAtSpecifierDefs[] = {
    {{"IMGREL"}, MCSymbolRefExpr::VK_COFF_IMGREL32},
};
constexpr auto COFFAtSpecifiers = BUILD_ENUM_STRINGS(COFFAtSpecifierDefs);

void RISCVMCAsmInfo::anchor() {}

RISCVMCAsmInfo::RISCVMCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  IsLittleEndian = TT.isLittleEndian();
  CodePointerSize = CalleeSaveStackSlotSize = TT.isArch64Bit() ? 8 : 4;
  CommentString = "#";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
  // The default symbol subtraction results in an ADD/SUB relocation pair.
  // Processing this relocation pair is problematic when linker relaxation is
  // enabled, so we follow binutils in using the R_RISCV_32_PCREL relocation
  // for the FDE initial location.
  DwarfFDERelSymbolSpec = ELF::R_RISCV_32_PCREL;
}

void RISCVMCAsmInfoCOFF::anchor() {}

RISCVMCAsmInfoCOFF::RISCVMCAsmInfoCOFF(const Triple &TT,
                                       const MCTargetOptions &Options)
    : MCAsmInfoGNUCOFF(Options) {
  IsLittleEndian = TT.isLittleEndian();
  CodePointerSize = CalleeSaveStackSlotSize = TT.isArch64Bit() ? 8 : 4;
  InternalSymbolPrefix = ".L";
  CommentString = "#";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  if (TT.isRISCV64()) {
    ExceptionsType = ExceptionHandling::WinEH;
    WinEHEncodingType = WinEH::EncodingType::RISCV64;
    // RVUW metadata has the same lifetime as the function it describes.  Use
    // real associative COMDATs even in the GNU environment so dead stripping
    // and COMDAT selection cannot orphan .pdata, .xdata, or handler data.
    HasCOFFAssociativeComdats = true;
  }
  DwarfRegNumForCFI = false;
  initializeAtSpecifiers(COFFAtSpecifiers);
}

void RISCVMCAsmInfoCOFF::printSpecifierExpr(
    raw_ostream &OS, const MCSpecifierExpr &Expr) const {
  auto S = Expr.getSpecifier();
  bool HasSpecifier = S != RISCV::S_None && S != RISCV::S_CALL_PLT;
  if (HasSpecifier)
    OS << '%' << RISCV::getSpecifierName(S) << '(';
  printExpr(OS, *Expr.getSubExpr());
  if (HasSpecifier)
    OS << ')';
}

bool RISCVMCAsmInfoCOFF::evaluateAsRelocatableImpl(
    const MCSpecifierExpr &Expr, MCValue &Res,
    const MCAssembler *Asm) const {
  if (!Expr.getSubExpr()->evaluateAsRelocatable(Res, Asm))
    return false;
  Res.setSpecifier(Expr.getSpecifier());
  return !Res.getSubSym();
}

void RISCVMCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                        const MCSpecifierExpr &Expr) const {
  auto S = Expr.getSpecifier();
  bool HasSpecifier = S != RISCV::S_None && S != RISCV::S_CALL_PLT;
  if (HasSpecifier)
    OS << '%' << RISCV::getSpecifierName(S) << '(';
  printExpr(OS, *Expr.getSubExpr());
  if (HasSpecifier)
    OS << ')';
}

RISCVMCAsmInfoDarwin::RISCVMCAsmInfoDarwin(const MCTargetOptions &Options)
    : MCAsmInfoDarwin(Options) {
  CodePointerSize = 4;
  InternalSymbolPrefix = "L";
  SeparatorString = "%%";
  CommentString = ";";
  AlignmentIsInBytes = false;
  SupportsDebugInformation = true;
  UseDataRegionDirectives = true;
  ExceptionsType = ExceptionHandling::DwarfCFI;
  Data16bitsDirective = "\t.half\t";
  Data32bitsDirective = "\t.word\t";
}

void RISCVMCAsmInfoDarwin::printSpecifierExpr(
    raw_ostream &OS, const MCSpecifierExpr &Expr) const {
  auto S = Expr.getSpecifier();
  bool HasSpecifier = S != RISCV::S_None && S != RISCV::S_CALL_PLT;
  if (HasSpecifier)
    OS << '%' << RISCV::getSpecifierName(S) << '(';
  printExpr(OS, *Expr.getSubExpr());
  if (HasSpecifier)
    OS << ')';
}
