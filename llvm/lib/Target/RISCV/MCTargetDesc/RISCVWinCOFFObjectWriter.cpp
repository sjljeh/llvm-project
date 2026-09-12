//===-- RISCVWinCOFFObjectWriter.cpp - RISC-V Windows COFF Writer --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCVMCTargetDesc.h"
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
    MCContext &, const MCValue &, const MCFixup &, bool,
    const MCAsmBackend &) const {
  // The PE/COFF specification defines RISC-V machine identifiers but does not
  // define the instruction relocation records needed for general linking.
  report_fatal_error("RISC-V Windows COFF relocations are not implemented");
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createRISCVWinCOFFObjectWriter(const Triple &TheTriple) {
  return std::make_unique<RISCVWinCOFFObjectWriter>(TheTriple);
}
