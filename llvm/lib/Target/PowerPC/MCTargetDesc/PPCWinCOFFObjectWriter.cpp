//===-- PPCWinCOFFObjectWriter.cpp - PowerPC WinCOFF Writer -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCFixupKinds.h"
#include "MCTargetDesc/PPCMCAsmInfo.h"
#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"

using namespace llvm;

namespace {
class PPCWinCOFFObjectWriter : public MCWinCOFFObjectTargetWriter {
public:
  PPCWinCOFFObjectWriter()
      : MCWinCOFFObjectTargetWriter(COFF::IMAGE_FILE_MACHINE_POWERPC) {}

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsCrossSection,
                        const MCAsmBackend &MAB) const override;
};
} // namespace

unsigned PPCWinCOFFObjectWriter::getRelocType(
    MCContext &Ctx, const MCValue &Target, const MCFixup &Fixup,
    bool IsCrossSection, const MCAsmBackend &MAB) const {
  unsigned FixupKind = Fixup.getKind();

  if (mc::isRelocation(FixupKind)) {
    Ctx.reportError(Fixup.getLoc(),
                    "ELF relocation specifier unsupported on COFF targets");
    return COFF::IMAGE_REL_PPC_ABSOLUTE;
  }

  switch (FixupKind) {
  default: {
    MCFixupKindInfo Info = MAB.getFixupKindInfo(Fixup.getKind());
    Ctx.reportError(Fixup.getLoc(), Twine("relocation type ") + Info.Name +
                                        " unsupported on COFF targets");
    return COFF::IMAGE_REL_PPC_ABSOLUTE;
  }
  case FK_Data_2:
    if (Fixup.isPCRel() || Target.getSpecifier() != PPC::S_None) {
      Ctx.reportError(Fixup.getLoc(),
                      "unsupported 16-bit PowerPC COFF relocation");
      return COFF::IMAGE_REL_PPC_ABSOLUTE;
    }
    return COFF::IMAGE_REL_PPC_ADDR16;
  case FK_Data_4:
    if (Fixup.isPCRel()) {
      Ctx.reportError(Fixup.getLoc(),
                      "unsupported 32-bit PowerPC COFF relocation");
      return COFF::IMAGE_REL_PPC_ABSOLUTE;
    }
    switch (Target.getSpecifier()) {
    default:
      Ctx.reportError(Fixup.getLoc(),
                      "relocation specifier unsupported on COFF targets");
      return COFF::IMAGE_REL_PPC_ABSOLUTE;
    case PPC::S_None:
      return COFF::IMAGE_REL_PPC_ADDR32;
    case PPC::S_IFGLUE:
      return COFF::IMAGE_REL_PPC_IFGLUE;
    case MCSymbolRefExpr::VK_COFF_IMGREL32:
      return COFF::IMAGE_REL_PPC_ADDR32NB;
    }
  case FK_SecRel_2:
    return COFF::IMAGE_REL_PPC_SECTION;
  case FK_SecRel_4:
    return COFF::IMAGE_REL_PPC_SECREL;
  case PPC::fixup_ppc_br24:
  case PPC::fixup_ppc_br24_notoc:
    return COFF::IMAGE_REL_PPC_REL24;
  case PPC::fixup_ppc_brcond14:
    return COFF::IMAGE_REL_PPC_REL14;
  case PPC::fixup_ppc_br24abs:
    return COFF::IMAGE_REL_PPC_ADDR24;
  case PPC::fixup_ppc_brcond14abs:
    return COFF::IMAGE_REL_PPC_ADDR14;
  case PPC::fixup_ppc_half16:
    switch (Target.getSpecifier()) {
    default:
      Ctx.reportError(Fixup.getLoc(),
                      "relocation specifier unsupported on COFF targets");
      return COFF::IMAGE_REL_PPC_ABSOLUTE;
    case PPC::S_None:
      return COFF::IMAGE_REL_PPC_ADDR16;
    case PPC::S_LO:
      return COFF::IMAGE_REL_PPC_REFLO;
    case PPC::S_HA:
      return COFF::IMAGE_REL_PPC_REFHI;
    case PPC::S_TOC:
      return COFF::IMAGE_REL_PPC_TOCREL16;
    }
  }
}

std::unique_ptr<MCObjectTargetWriter> llvm::createPPCWinCOFFObjectWriter() {
  return std::make_unique<PPCWinCOFFObjectWriter>();
}
