//===-- RISCVWinCOFFObjectWriter.cpp - RISC-V Windows COFF Writer --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCVMCTargetDesc.h"
#include "RISCVFixupKinds.h"
#include "RISCVMCAsmInfo.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

class RISCVWinCOFFObjectWriter : public MCWinCOFFObjectTargetWriter {
public:
  explicit RISCVWinCOFFObjectWriter(const Triple &TheTriple);
  ~RISCVWinCOFFObjectWriter() override = default;

  unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                        const MCFixup &Fixup, bool IsCrossSection,
                        const MCAsmBackend &MAB) const override;
};

static unsigned getMachineFromTriple(const Triple &TheTriple) {
  if (TheTriple.isRISCV32())
    return COFF::IMAGE_FILE_MACHINE_RISCV32;
  if (TheTriple.isRISCV64())
    return COFF::IMAGE_FILE_MACHINE_RISCV64;
  llvm_unreachable("unsupported RISC-V Windows COFF triple");
}

} // namespace

RISCVWinCOFFObjectWriter::RISCVWinCOFFObjectWriter(const Triple &TheTriple)
    : MCWinCOFFObjectTargetWriter(getMachineFromTriple(TheTriple)) {}

unsigned RISCVWinCOFFObjectWriter::getRelocType(
    MCContext &Ctx, const MCValue &Target, const MCFixup &Fixup,
    bool IsCrossSection, const MCAsmBackend &MAB) const {
  unsigned Kind = Fixup.getKind();
  bool IsPCRel = Fixup.isPCRel();

  if (mc::isRelocation(Kind)) {
    Ctx.reportError(Fixup.getLoc(), "ELF relocation specifier unsupported on COFF targets");
    return COFF::IMAGE_REL_RISCV_ABSOLUTE;
  }

  if (IsCrossSection) {
    if (IsPCRel || (Kind != FK_Data_4 && Kind != FK_Data_8)) {
      Ctx.reportError(Fixup.getLoc(), "cannot represent this expression");
      return COFF::IMAGE_REL_RISCV_ABSOLUTE;
    }
    Kind = FK_Data_4;
    IsPCRel = true;
  }

  switch (Kind) {
  default: {
    MCFixupKindInfo Info = MAB.getFixupKindInfo(Fixup.getKind());
    Ctx.reportError(Fixup.getLoc(), Twine("relocation type ") + Info.Name + " unsupported on COFF targets");
    return COFF::IMAGE_REL_RISCV_ABSOLUTE;
  }
  case FK_Data_4:
    if (IsPCRel)
      return COFF::IMAGE_REL_RISCV_REL32;
    if (Target.getSpecifier() == MCSymbolRefExpr::VK_COFF_IMGREL32)
      return COFF::IMAGE_REL_RISCV_ADDR32NB;
    if (Target.getSpecifier() == RISCV::S_None)
      return COFF::IMAGE_REL_RISCV_ADDR32;
    Ctx.reportError(Fixup.getLoc(),
                    "relocation specifier unsupported on COFF targets");
    return COFF::IMAGE_REL_RISCV_ABSOLUTE;
  case FK_Data_8:
    if (IsPCRel) {
      Ctx.reportError(Fixup.getLoc(),
                      "64-bit PC-relative data relocations unsupported on "
                      "COFF targets");
      return COFF::IMAGE_REL_RISCV_ABSOLUTE;
    }
    if (Target.getSpecifier() != RISCV::S_None) {
      Ctx.reportError(Fixup.getLoc(),
                      "relocation specifier unsupported on COFF targets");
      return COFF::IMAGE_REL_RISCV_ABSOLUTE;
    }
    return COFF::IMAGE_REL_RISCV_ADDR64;
  case FK_SecRel_2:
    return COFF::IMAGE_REL_RISCV_SECTION;
  case FK_SecRel_4:
    return COFF::IMAGE_REL_RISCV_SECREL;
  case RISCV::fixup_riscv_branch:
    return COFF::IMAGE_REL_RISCV_BRANCH;
  case RISCV::fixup_riscv_jal:
    return COFF::IMAGE_REL_RISCV_JAL;
  case RISCV::fixup_riscv_call:
  case RISCV::fixup_riscv_call_plt:
    return COFF::IMAGE_REL_RISCV_CALL;
  case RISCV::fixup_riscv_pcrel_hi20:
    return COFF::IMAGE_REL_RISCV_PCREL_HI20;
  case RISCV::fixup_riscv_pcrel_lo12_i:
    return COFF::IMAGE_REL_RISCV_PCREL_LO12_I;
  case RISCV::fixup_riscv_pcrel_lo12_s:
    return COFF::IMAGE_REL_RISCV_PCREL_LO12_S;
  case RISCV::fixup_riscv_hi20:
    return COFF::IMAGE_REL_RISCV_HI20;
  case RISCV::fixup_riscv_lo12_i:
    return COFF::IMAGE_REL_RISCV_LO12_I;
  case RISCV::fixup_riscv_lo12_s:
    return COFF::IMAGE_REL_RISCV_LO12_S;
  case RISCV::fixup_riscv_rvc_jump:
    return COFF::IMAGE_REL_RISCV_RVC_JUMP;
  case RISCV::fixup_riscv_rvc_branch:
    return COFF::IMAGE_REL_RISCV_RVC_BRANCH;
  }
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createRISCVWinCOFFObjectWriter(const Triple &TheTriple) {
  return std::make_unique<RISCVWinCOFFObjectWriter>(TheTriple);
}
