//===-- AlphaWinCOFFObjectWriter.cpp - Alpha WinCOFF Writer ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaMCTargetDesc.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"

using namespace llvm;

namespace {

class AlphaWinCOFFObjectWriter : public MCWinCOFFObjectTargetWriter {
public:
  AlphaWinCOFFObjectWriter(unsigned Machine)
      : MCWinCOFFObjectTargetWriter(Machine) {}

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsCrossSection,
                        const MCAsmBackend &MAB) const override {
    if (mc::isRelocation(Fixup.getKind())) {
      Ctx.reportError(Fixup.getLoc(),
                      "ELF relocation specifier unsupported on COFF targets");
      return COFF::IMAGE_REL_ALPHA_ABSOLUTE;
    }

    switch (Fixup.getKind()) {
    default:
      Ctx.reportError(Fixup.getLoc(), "unsupported relocation type");
      return COFF::IMAGE_REL_ALPHA_ABSOLUTE;
    case FK_Data_4:
      if (Fixup.isPCRel()) {
        Ctx.reportError(Fixup.getLoc(),
                        "32-bit PC-relative data relocations unsupported on "
                        "Alpha COFF targets");
        return COFF::IMAGE_REL_ALPHA_ABSOLUTE;
      }
      switch (Target.getSpecifier()) {
      default:
        Ctx.reportError(Fixup.getLoc(),
                        "relocation specifier unsupported on COFF targets");
        return COFF::IMAGE_REL_ALPHA_ABSOLUTE;
      case 0:
        return COFF::IMAGE_REL_ALPHA_REFLONG;
      case MCSymbolRefExpr::VK_COFF_IMGREL32:
        return COFF::IMAGE_REL_ALPHA_REFLONGNB;
      }
    case Alpha::fixup_Alpha_Branch:
      return COFF::IMAGE_REL_ALPHA_BRADDR;
    case Alpha::fixup_Alpha_REFHI:
      return COFF::IMAGE_REL_ALPHA_REFHI;
    case Alpha::fixup_Alpha_REFLO:
      return COFF::IMAGE_REL_ALPHA_REFLO;
    case FK_Data_8:
      if (Fixup.isPCRel()) {
        Ctx.reportError(Fixup.getLoc(),
                        "64-bit PC-relative data relocations unsupported on "
                        "Alpha COFF targets");
        return COFF::IMAGE_REL_ALPHA_ABSOLUTE;
      }
      if (Target.getSpecifier() != 0) {
        Ctx.reportError(Fixup.getLoc(),
                        "relocation specifier unsupported on COFF targets");
        return COFF::IMAGE_REL_ALPHA_ABSOLUTE;
      }
      return COFF::IMAGE_REL_ALPHA_REFQUAD;
    case FK_SecRel_2:
      return COFF::IMAGE_REL_ALPHA_SECTION;
    case FK_SecRel_4:
      return COFF::IMAGE_REL_ALPHA_SECREL;
    }
  }
};

} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createAlphaWinCOFFObjectWriter(unsigned Machine) {
  return std::make_unique<AlphaWinCOFFObjectWriter>(Machine);
}
