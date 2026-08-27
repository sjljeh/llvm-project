//===-- AlphaInstPrinter.cpp - Print Alpha MC instructions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaInstPrinter.h"
#include "AlphaMCTargetDesc.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "AlphaGenAsmWriter.inc"

void AlphaInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  markup(OS, Markup::Register) << getRegisterName(Reg);
}

void AlphaInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot,
                                 const MCSubtargetInfo &STI,
                                 raw_ostream &OS) {
  // GAS accepts the canonical operand-less alias, and the minimal Alpha
  // parser supports that spelling for round-tripping generated assembly.
  if (MI->getOpcode() == Alpha::RETDAG || MI->getOpcode() == Alpha::RETDAGp) {
    OS << "\tret";
    printAnnotation(OS, Annot);
    return;
  }
  printInstruction(MI, Address, OS);
  printAnnotation(OS, Annot);
}

void AlphaInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    raw_ostream &OS) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    printRegName(OS, Op.getReg());
    return;
  }
  if (Op.isImm()) {
    markup(OS, Markup::Immediate) << formatImm(Op.getImm());
    return;
  }
  if (Op.isExpr()) {
    MAI.printExpr(OS, *Op.getExpr());
    return;
  }
  llvm_unreachable("unknown Alpha operand kind");
}
