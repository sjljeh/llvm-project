//===-- AlphaAsmPrinter.cpp - Alpha LLVM assembly writer ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to GAS-format Alpha assembly language.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "asm-printer"
#include "Alpha.h"
#include "AlphaInstrInfo.h"
#include "AlphaTargetMachine.h"
#include "TargetInfo/AlphaTargetInfo.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/IR/Mangler.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {
  struct AlphaAsmPrinter : public AsmPrinter {
    /// Unique incrementer for label values for referencing Global values.
    ///

    explicit AlphaAsmPrinter(TargetMachine &tm,
                             std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(tm, std::move(Streamer), ID) {}

    StringRef getPassName() const override {
      return "Alpha Assembly Printer";
    }
    void printInstruction(const MachineInstr *MI, raw_ostream &O);
    void emitInstruction(const MachineInstr *MI) override {
      if (MI->getOpcode() == Alpha::SEH_PrologEnd) {
        OutStreamer->emitWinCFIEndProlog();
        return;
      }
      if (MI->getOpcode() == Alpha::CATCHRET) {
        const MachineBasicBlock *TargetMBB = MI->getOperand(0).getMBB();
        const MCSymbol *TargetSym = TargetMBB->isEHContTarget()
                                        ? TargetMBB->getEHContSymbol()
                                        : TargetMBB->getSymbol();
        const MCSymbolRefExpr *Target =
            MCSymbolRefExpr::create(TargetSym, OutContext);
        EmitToStreamer(*OutStreamer, MCInstBuilder(Alpha::LDAHr)
                                           .addReg(Alpha::R0)
                                           .addExpr(Target)
                                           .addReg(Alpha::R31));
        EmitToStreamer(*OutStreamer, MCInstBuilder(Alpha::LDAr)
                                           .addReg(Alpha::R0)
                                           .addExpr(Target)
                                           .addReg(Alpha::R0));
        EmitToStreamer(*OutStreamer, MCInstBuilder(Alpha::RETDAGp));
        return;
      }
      if (MI->getOpcode() == Alpha::CLEANUPRET) {
        EmitToStreamer(*OutStreamer, MCInstBuilder(Alpha::RETDAGp));
        return;
      }

      MCInst TmpInst;
      TmpInst.setOpcode(MI->getOpcode());
      for (const MachineOperand &MO : MI->operands()) {
        if (MO.isReg()) {
          if (MO.getReg())
            TmpInst.addOperand(MCOperand::createReg(MO.getReg()));
        } else if (MO.isImm()) {
          TmpInst.addOperand(MCOperand::createImm(MO.getImm()));
        } else if (MO.isMBB()) {
          TmpInst.addOperand(MCOperand::createExpr(
              MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), OutContext)));
        } else if (MO.isGlobal()) {
          TmpInst.addOperand(MCOperand::createExpr(
              MCSymbolRefExpr::create(getSymbol(MO.getGlobal()), OutContext)));
        } else if (MO.isCPI()) {
          TmpInst.addOperand(MCOperand::createExpr(
              MCSymbolRefExpr::create(GetCPISymbol(MO.getIndex()), OutContext)));
        } else if (MO.isJTI()) {
          TmpInst.addOperand(MCOperand::createExpr(
              MCSymbolRefExpr::create(GetJTISymbol(MO.getIndex()), OutContext)));
        } else if (MO.isBlockAddress()) {
          TmpInst.addOperand(MCOperand::createExpr(MCSymbolRefExpr::create(
              GetBlockAddressSymbol(MO.getBlockAddress()), OutContext)));
        } else if (MO.isSymbol()) {
          TmpInst.addOperand(MCOperand::createExpr(MCSymbolRefExpr::create(
              OutContext.getOrCreateSymbol(MO.getSymbolName()), OutContext)));
        }
      }
      EmitToStreamer(*OutStreamer, TmpInst);
    }
    static const char *getRegisterName(MCRegister RegNo);

    void printOp(const MachineOperand &MO, raw_ostream &O);
    void printOperand(const MachineInstr *MI, int opNum, raw_ostream &O);
    void emitFunctionBodyStart() override;
    void emitFunctionBodyEnd() override;
    void emitStartOfAsmFile(Module &M) override;

    bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                         const char *ExtraCode, raw_ostream &O) override;
    bool PrintAsmMemoryOperand(const MachineInstr *MI,
                               unsigned OpNo, const char *ExtraCode,
                               raw_ostream &O) override;

    static char ID;
  };
} // end of anonymous namespace

char AlphaAsmPrinter::ID = 0;

void AlphaAsmPrinter::printInstruction(const MachineInstr *MI, raw_ostream &O) {
  O << "\t# alpha opcode " << MI->getOpcode();
}

const char *AlphaAsmPrinter::getRegisterName(MCRegister RegNo) {
  static thread_local std::string Name;
  Name = AlphaRegisterInfo::getPrettyName(RegNo);
  return Name.c_str();
}

void AlphaAsmPrinter::printOperand(const MachineInstr *MI, int opNum,
                                   raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(opNum);
  if (MO.isReg()) {
    assert(Register::isPhysicalRegister(MO.getReg()) &&
           "Not physreg??");
    O << getRegisterName(MO.getReg());
  } else if (MO.isImm()) {
    O << MO.getImm();
    assert(MO.getImm() < (1 << 30));
  } else {
    printOp(MO, O);
  }
}


void AlphaAsmPrinter::printOp(const MachineOperand &MO, raw_ostream &O) {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << getRegisterName(MO.getReg());
    return;

  case MachineOperand::MO_Immediate:
    assert(0 && "printOp() does not handle immediate values");
    return;

  case MachineOperand::MO_MachineBasicBlock:
    O << *MO.getMBB()->getSymbol();
    return;

  case MachineOperand::MO_ConstantPoolIndex:
    O << *GetCPISymbol(MO.getIndex());
    return;

  case MachineOperand::MO_ExternalSymbol:
    O << MO.getSymbolName();
    return;

  case MachineOperand::MO_GlobalAddress:
    O << *getSymbol(MO.getGlobal());
    return;

  case MachineOperand::MO_JumpTableIndex:
    O << *GetJTISymbol(MO.getIndex());
    return;

  default:
    O << "<unknown operand type: " << MO.getType() << ">";
    return;
  }
}

/// EmitFunctionBodyStart - Targets can override this to emit stuff before
/// the first basic block in the function.
void AlphaAsmPrinter::emitFunctionBodyStart() {
  for (MachineBasicBlock &MBB : *MF)
    if (!MBB.isEntryBlock())
      MBB.setLabelMustBeEmitted();
}

/// EmitFunctionBodyEnd - Targets can override this to emit stuff after
/// the last basic block in the function.
void AlphaAsmPrinter::emitFunctionBodyEnd() {
}

void AlphaAsmPrinter::emitStartOfAsmFile(Module &M) {
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
///
bool AlphaAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &O) {
  printOperand(MI, OpNo, O);
  return false;
}

bool AlphaAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                            unsigned OpNo, const char *ExtraCode,
                                            raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier.
  O << "0(";
  printOperand(MI, OpNo, O);
  O << ")";
  return false;
}

// Force static initialization.
extern "C" void LLVMInitializeAlphaAsmPrinter() { 
  RegisterAsmPrinter<AlphaAsmPrinter> X(getTheAlphaTarget());
}
