//=====-- AlphaMCAsmInfo.h - Alpha asm properties -------------*- C++ -*--====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the AlphaMCAsmInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef ALPHATARGETASMINFO_H
#define ALPHATARGETASMINFO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmInfoCOFF.h"
#include "llvm/MC/MCAsmInfoELF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
  class Target;
  class MCAssembler;
  class MCSpecifierExpr;
  class MCValue;

  struct AlphaMCAsmInfo : public MCAsmInfoELF {
    explicit AlphaMCAsmInfo(const Triple &TT, const MCTargetOptions &Options);
    void printSpecifierExpr(raw_ostream &OS,
                            const MCSpecifierExpr &Expr) const override;
    bool evaluateAsRelocatableImpl(const MCSpecifierExpr &Expr, MCValue &Res,
                                   const MCAssembler *Asm) const override;
  };

  struct AlphaMCAsmInfoMicrosoftCOFF : public MCAsmInfoMicrosoft {
    explicit AlphaMCAsmInfoMicrosoftCOFF(const Triple &TT,
                                        const MCTargetOptions &Options);
  };

} // namespace llvm

#endif
