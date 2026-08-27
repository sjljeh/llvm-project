//===-- AlphaInstPrinter.h - Print Alpha MC instructions -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ALPHA_MCTARGETDESC_ALPHA_INSTPRINTER_H
#define LLVM_LIB_TARGET_ALPHA_MCTARGETDESC_ALPHA_INSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class AlphaInstPrinter final : public MCInstPrinter {
public:
  AlphaInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                   const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  std::pair<const char *, uint64_t>
  getMnemonic(const MCInst &MI) const override;
  void printInstruction(const MCInst *MI, uint64_t Address, raw_ostream &OS);
  static const char *getRegisterName(MCRegister Reg);

  void printRegName(raw_ostream &OS, MCRegister Reg) override;
  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI, raw_ostream &OS) override;

private:
  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &OS);
};

} // end namespace llvm

#endif
