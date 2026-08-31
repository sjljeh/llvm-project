//===-- AlphaELFObjectWriter.cpp - Alpha ELF writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {
class AlphaELFObjectWriter final : public MCELFObjectTargetWriter {
public:
  explicit AlphaELFObjectWriter(uint8_t OSABI)
      : MCELFObjectTargetWriter(/*Is64Bit=*/true, OSABI, ELF::EM_ALPHA,
                                /*HasRelocationAddend=*/true) {}

  bool needsRelocateWithSymbol(const MCValue &, unsigned Type) const override {
    switch (Type) {
    case ELF::R_ALPHA_LITERAL:
    case ELF::R_ALPHA_BRSGP:
    case ELF::R_ALPHA_TLSGD:
    case ELF::R_ALPHA_TLSLDM:
    case ELF::R_ALPHA_DTPMOD64:
    case ELF::R_ALPHA_GOTDTPREL:
    case ELF::R_ALPHA_DTPREL64:
    case ELF::R_ALPHA_DTPRELHI:
    case ELF::R_ALPHA_DTPRELLO:
    case ELF::R_ALPHA_DTPREL16:
    case ELF::R_ALPHA_GOTTPREL:
    case ELF::R_ALPHA_TPREL64:
    case ELF::R_ALPHA_TPRELHI:
    case ELF::R_ALPHA_TPRELLO:
    case ELF::R_ALPHA_TPREL16:
      return true;
    default:
      return false;
    }
  }

  unsigned getRelocType(const MCFixup &Fixup, const MCValue &Target,
                        bool IsPCRel) const override {
    if (Target.getSpecifier() == Alpha::S_GPREL32)
      return ELF::R_ALPHA_GPREL32;
    switch (Fixup.getKind()) {
    case FK_Data_2:
      if (IsPCRel)
        return ELF::R_ALPHA_SREL16;
      report_fatal_error("Alpha ELF has no 16-bit absolute relocation");
    case FK_Data_4:
      return IsPCRel ? ELF::R_ALPHA_SREL32 : ELF::R_ALPHA_REFLONG;
    case FK_Data_8:
      return IsPCRel ? ELF::R_ALPHA_SREL64 : ELF::R_ALPHA_REFQUAD;
    case Alpha::fixup_Alpha_Branch:
      return ELF::R_ALPHA_BRADDR;
    case Alpha::fixup_Alpha_REFHI:
      return ELF::R_ALPHA_GPRELHIGH;
    case Alpha::fixup_Alpha_REFLO:
      return ELF::R_ALPHA_GPRELLOW;
    case Alpha::fixup_Alpha_LITERAL:
      return ELF::R_ALPHA_LITERAL;
    case Alpha::fixup_Alpha_LITUSE:
      return ELF::R_ALPHA_LITUSE;
    case Alpha::fixup_Alpha_GPDISP:
      return ELF::R_ALPHA_GPDISP;
    case Alpha::fixup_Alpha_BRSGP:
      return ELF::R_ALPHA_BRSGP;
    case Alpha::fixup_Alpha_HINT:
      return ELF::R_ALPHA_HINT;
    case Alpha::fixup_Alpha_GPREL16:
      return ELF::R_ALPHA_GPREL16;
    case Alpha::fixup_Alpha_TLSGD:
      return ELF::R_ALPHA_TLSGD;
    case Alpha::fixup_Alpha_TLSLDM:
      return ELF::R_ALPHA_TLSLDM;
    case Alpha::fixup_Alpha_GOTDTPREL:
      return ELF::R_ALPHA_GOTDTPREL;
    case Alpha::fixup_Alpha_DTPRELHI:
      return ELF::R_ALPHA_DTPRELHI;
    case Alpha::fixup_Alpha_DTPRELLO:
      return ELF::R_ALPHA_DTPRELLO;
    case Alpha::fixup_Alpha_DTPREL16:
      return ELF::R_ALPHA_DTPREL16;
    case Alpha::fixup_Alpha_GOTTPREL:
      return ELF::R_ALPHA_GOTTPREL;
    case Alpha::fixup_Alpha_TPRELHI:
      return ELF::R_ALPHA_TPRELHI;
    case Alpha::fixup_Alpha_TPRELLO:
      return ELF::R_ALPHA_TPRELLO;
    case Alpha::fixup_Alpha_TPREL16:
      return ELF::R_ALPHA_TPREL16;
    default:
      llvm_unreachable("unsupported Alpha ELF relocation");
    }
  }
};
} // end anonymous namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createAlphaELFObjectWriter(const Triple &TT) {
  return std::make_unique<AlphaELFObjectWriter>(
      MCELFObjectTargetWriter::getOSABI(TT.getOS()));
}
