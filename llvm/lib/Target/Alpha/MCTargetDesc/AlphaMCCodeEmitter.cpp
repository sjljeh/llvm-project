//===-- AlphaMCCodeEmitter.cpp - Alpha machine code emitter ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

namespace {

class AlphaMCCodeEmitter : public MCCodeEmitter {
  MCContext &Ctx;

public:
  AlphaMCCodeEmitter(const MCInstrInfo &MCII, MCContext &Ctx) : Ctx(Ctx) {}

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;

private:
  uint32_t getBinaryCodeForInstr(const MCInst &MI,
                                 SmallVectorImpl<MCFixup> &Fixups) const;
  unsigned getRegisterNumber(const MCOperand &MO) const;
  int64_t getImmediate(const MCInst &MI, unsigned OpNo) const;
};

} // end anonymous namespace

unsigned AlphaMCCodeEmitter::getRegisterNumber(const MCOperand &MO) const {
  if (!MO.isReg())
    Ctx.reportError(SMLoc(), "expected Alpha register operand");

  switch (MO.getReg()) {
  case Alpha::R0: return 0;
  case Alpha::R1: return 1;
  case Alpha::R2: return 2;
  case Alpha::R3: return 3;
  case Alpha::R4: return 4;
  case Alpha::R5: return 5;
  case Alpha::R6: return 6;
  case Alpha::R7: return 7;
  case Alpha::R8: return 8;
  case Alpha::R9: return 9;
  case Alpha::R10: return 10;
  case Alpha::R11: return 11;
  case Alpha::R12: return 12;
  case Alpha::R13: return 13;
  case Alpha::R14: return 14;
  case Alpha::R15: return 15;
  case Alpha::R16: return 16;
  case Alpha::R17: return 17;
  case Alpha::R18: return 18;
  case Alpha::R19: return 19;
  case Alpha::R20: return 20;
  case Alpha::R21: return 21;
  case Alpha::R22: return 22;
  case Alpha::R23: return 23;
  case Alpha::R24: return 24;
  case Alpha::R25: return 25;
  case Alpha::R26: return 26;
  case Alpha::R27: return 27;
  case Alpha::R28: return 28;
  case Alpha::R29: return 29;
  case Alpha::R30: return 30;
  case Alpha::R31: return 31;
  case Alpha::F0: return 0;
  case Alpha::F1: return 1;
  case Alpha::F2: return 2;
  case Alpha::F3: return 3;
  case Alpha::F4: return 4;
  case Alpha::F5: return 5;
  case Alpha::F6: return 6;
  case Alpha::F7: return 7;
  case Alpha::F8: return 8;
  case Alpha::F9: return 9;
  case Alpha::F10: return 10;
  case Alpha::F11: return 11;
  case Alpha::F12: return 12;
  case Alpha::F13: return 13;
  case Alpha::F14: return 14;
  case Alpha::F15: return 15;
  case Alpha::F16: return 16;
  case Alpha::F17: return 17;
  case Alpha::F18: return 18;
  case Alpha::F19: return 19;
  case Alpha::F20: return 20;
  case Alpha::F21: return 21;
  case Alpha::F22: return 22;
  case Alpha::F23: return 23;
  case Alpha::F24: return 24;
  case Alpha::F25: return 25;
  case Alpha::F26: return 26;
  case Alpha::F27: return 27;
  case Alpha::F28: return 28;
  case Alpha::F29: return 29;
  case Alpha::F30: return 30;
  case Alpha::F31: return 31;
  default:
    Ctx.reportError(SMLoc(), "unsupported Alpha register operand");
    return 0;
  }
}

int64_t AlphaMCCodeEmitter::getImmediate(const MCInst &MI,
                                         unsigned OpNo) const {
  const MCOperand &MO = MI.getOperand(OpNo);
  if (!MO.isImm())
    Ctx.reportError(MI.getLoc(), "expected Alpha immediate operand");
  return MO.getImm();
}

uint32_t AlphaMCCodeEmitter::getBinaryCodeForInstr(
    const MCInst &MI, SmallVectorImpl<MCFixup> &Fixups) const {
  auto GetFixupKind = [](const MCExpr *Expr,
                         MCFixupKind DefaultKind) -> MCFixupKind {
    const auto *Specifier = dyn_cast<MCSpecifierExpr>(Expr);
    if (!Specifier)
      return DefaultKind;
    switch (static_cast<Alpha::Specifier>(Specifier->getSpecifier())) {
    case Alpha::S_LITERAL:
      return Alpha::fixup_Alpha_LITERAL;
    case Alpha::S_LITUSE_ADDR:
    case Alpha::S_LITUSE_BASE:
    case Alpha::S_LITUSE_BYTOFF:
    case Alpha::S_LITUSE_JSR:
    case Alpha::S_LITUSE_TLSGD:
    case Alpha::S_LITUSE_TLSLDM:
    case Alpha::S_LITUSE_JSRDIRECT:
      return DefaultKind;
    case Alpha::S_GPDISP:
      return Alpha::fixup_Alpha_GPDISP;
    case Alpha::S_GPRELHIGH:
      return Alpha::fixup_Alpha_REFHI;
    case Alpha::S_GPRELLOW:
      return Alpha::fixup_Alpha_REFLO;
    case Alpha::S_GPREL16:
      return Alpha::fixup_Alpha_GPREL16;
    case Alpha::S_GPREL32:
      return DefaultKind;
    case Alpha::S_BRSGP:
      return Alpha::fixup_Alpha_BRSGP;
    case Alpha::S_TLSGD:
      return Alpha::fixup_Alpha_TLSGD;
    case Alpha::S_TLSLDM:
      return Alpha::fixup_Alpha_TLSLDM;
    case Alpha::S_GOTDTPREL:
      return Alpha::fixup_Alpha_GOTDTPREL;
    case Alpha::S_DTPRELHI:
      return Alpha::fixup_Alpha_DTPRELHI;
    case Alpha::S_DTPRELLO:
      return Alpha::fixup_Alpha_DTPRELLO;
    case Alpha::S_DTPREL16:
      return Alpha::fixup_Alpha_DTPREL16;
    case Alpha::S_GOTTPREL:
      return Alpha::fixup_Alpha_GOTTPREL;
    case Alpha::S_TPRELHI:
      return Alpha::fixup_Alpha_TPRELHI;
    case Alpha::S_TPRELLO:
      return Alpha::fixup_Alpha_TPRELLO;
    case Alpha::S_TPREL16:
      return Alpha::fixup_Alpha_TPREL16;
    case Alpha::S_None:
      return DefaultKind;
    }
    llvm_unreachable("invalid Alpha relocation specifier");
  };

  auto GetFixupExpr = [&](const MCExpr *Expr) -> const MCExpr * {
    const auto *Specifier = dyn_cast<MCSpecifierExpr>(Expr);
    if (!Specifier)
      return Expr;
    switch (static_cast<Alpha::Specifier>(Specifier->getSpecifier())) {
    case Alpha::S_GPDISP:
      return MCConstantExpr::create(4, Ctx);
    default:
      return Specifier->getSubExpr();
    }
  };

  bool HasLituse = false;
  if (Ctx.getTargetTriple().isOSBinFormatELF()) {
    for (const MCOperand &Op : MI) {
      if (!Op.isExpr())
        continue;
      const auto *Specifier = dyn_cast<MCSpecifierExpr>(Op.getExpr());
      if (!Specifier)
        continue;
      unsigned Use;
      switch (static_cast<Alpha::Specifier>(Specifier->getSpecifier())) {
      case Alpha::S_LITUSE_ADDR:
        Use = ELF::LITUSE_ALPHA_ADDR;
        break;
      case Alpha::S_LITUSE_BASE:
        Use = ELF::LITUSE_ALPHA_BASE;
        break;
      case Alpha::S_LITUSE_BYTOFF:
        Use = ELF::LITUSE_ALPHA_BYTOFF;
        break;
      case Alpha::S_LITUSE_JSR:
        Use = ELF::LITUSE_ALPHA_JSR;
        break;
      case Alpha::S_LITUSE_TLSGD:
        Use = ELF::LITUSE_ALPHA_TLSGD;
        break;
      case Alpha::S_LITUSE_TLSLDM:
        Use = ELF::LITUSE_ALPHA_TLSLDM;
        break;
      case Alpha::S_LITUSE_JSRDIRECT:
        Use = ELF::LITUSE_ALPHA_JSRDIRECT;
        break;
      default:
        continue;
      }
      Fixups.push_back(MCFixup::create(0, MCConstantExpr::create(Use, Ctx),
                                       Alpha::fixup_Alpha_LITUSE));
      HasLituse = true;
      break;
    }
    if (!HasLituse &&
        (MI.getOpcode() == Alpha::JSRl || MI.getOpcode() == Alpha::JSRs)) {
      Fixups.push_back(
          MCFixup::create(0, MCConstantExpr::create(ELF::LITUSE_ALPHA_JSR, Ctx),
                          Alpha::fixup_Alpha_LITUSE));
    }
  }

  auto EncodeMem = [&](unsigned Opcode) -> uint32_t {
    unsigned Ra = getRegisterNumber(MI.getOperand(0));
    unsigned Rb = getRegisterNumber(MI.getOperand(2));
    const MCOperand &DispOp = MI.getOperand(1);
    if (DispOp.isExpr()) {
      MCFixupKind Kind =
          GetFixupKind(DispOp.getExpr(), Alpha::fixup_Alpha_REFLO);
      if (Kind == Alpha::fixup_Alpha_GPDISP && Opcode == 0x08)
        return (Opcode << 26) | (Ra << 21) | (Rb << 16);
      Fixups.push_back(
          MCFixup::create(0, GetFixupExpr(DispOp.getExpr()), Kind, false));
      return (Opcode << 26) | (Ra << 21) | (Rb << 16);
    }
    int64_t Disp = getImmediate(MI, 1);
    if (!isInt<16>(Disp))
      Ctx.reportError(MI.getLoc(), "Alpha memory displacement out of range");
    return (Opcode << 26) | (Ra << 21) | (Rb << 16) |
           (static_cast<uint32_t>(Disp) & 0xffff);
  };

  auto EncodeMemFixup = [&](unsigned Opcode, unsigned FixupKind) -> uint32_t {
    unsigned Ra = getRegisterNumber(MI.getOperand(0));
    unsigned Rb = getRegisterNumber(MI.getOperand(2));
    const MCOperand &DispOp = MI.getOperand(1);
    if (DispOp.isExpr()) {
      MCFixupKind Kind = GetFixupKind(DispOp.getExpr(), FixupKind);
      if (Kind == Alpha::fixup_Alpha_GPDISP && Opcode == 0x08)
        return (Opcode << 26) | (Ra << 21) | (Rb << 16);
      Fixups.push_back(
          MCFixup::create(0, GetFixupExpr(DispOp.getExpr()), Kind, false));
      return (Opcode << 26) | (Ra << 21) | (Rb << 16);
    }
    int64_t Disp = getImmediate(MI, 1);
    if (!isInt<16>(Disp))
      Ctx.reportError(MI.getLoc(), "Alpha memory displacement out of range");
    return (Opcode << 26) | (Ra << 21) | (Rb << 16) |
           (static_cast<uint32_t>(Disp) & 0xffff);
  };

  auto EncodeOperate = [&](unsigned Opcode, unsigned Function) -> uint32_t {
    unsigned Rc = getRegisterNumber(MI.getOperand(0));
    unsigned Ra = getRegisterNumber(MI.getOperand(1));
    uint32_t Bits = (Opcode << 26) | (Ra << 21) | (Function << 5) | Rc;

    if (MI.getOperand(2).isReg()) {
      unsigned Rb = getRegisterNumber(MI.getOperand(2));
      return Bits | (Rb << 16);
    }

    int64_t Lit = getImmediate(MI, 2);
    if (Lit < 0 || Lit > 255)
      Ctx.reportError(MI.getLoc(), "Alpha literal operand out of range");
    return Bits | ((static_cast<uint32_t>(Lit) & 0xff) << 13) | (1 << 12);
  };

  auto EncodeCmovLit = [&](unsigned Opcode, unsigned Function) -> uint32_t {
    unsigned Rc = getRegisterNumber(MI.getOperand(0));
    unsigned Ra = getRegisterNumber(MI.getOperand(1));
    int64_t Lit = getImmediate(MI, 2);
    if (Lit < 0 || Lit > 255)
      Ctx.reportError(MI.getLoc(), "Alpha literal operand out of range");
    return (Opcode << 26) | (Ra << 21) |
           ((static_cast<uint32_t>(Lit) & 0xff) << 13) | (1 << 12) |
           (Function << 5) | Rc;
  };

  auto EncodeCmovReg = [&](unsigned Opcode, unsigned Function) -> uint32_t {
    unsigned Rc = getRegisterNumber(MI.getOperand(0));
    unsigned Ra = getRegisterNumber(MI.getOperand(1));
    unsigned Rb = getRegisterNumber(MI.getOperand(2));
    return (Opcode << 26) | (Ra << 21) | (Rb << 16) | (Function << 5) | Rc;
  };

  auto EncodeFP = [&](unsigned Opcode, unsigned Function, unsigned Fa,
                      unsigned Fb, unsigned Fc) -> uint32_t {
    return (Opcode << 26) | (Fa << 21) | (Fb << 16) | (Function << 5) | Fc;
  };

  auto EncodeOperate2 = [&](unsigned Opcode, unsigned Function) -> uint32_t {
    unsigned Rc = getRegisterNumber(MI.getOperand(0));
    unsigned Rb = getRegisterNumber(MI.getOperand(1));
    return (Opcode << 26) | (31 << 21) | (Rb << 16) | (Function << 5) | Rc;
  };

  auto EncodeBranch = [&](unsigned Opcode, unsigned Ra, int64_t Disp) -> uint32_t {
    if (!isInt<21>(Disp))
      Ctx.reportError(MI.getLoc(), "Alpha branch displacement out of range");
    return (Opcode << 26) | (Ra << 21) |
            (static_cast<uint32_t>(Disp) & 0x1fffff);
  };

  auto EncodeBranchOperand = [&](unsigned Opcode, unsigned Ra,
                                 unsigned OpNo) -> uint32_t {
    const MCOperand &MO = MI.getOperand(OpNo);
    if (MO.isExpr()) {
      MCFixupKind Kind = GetFixupKind(MO.getExpr(), Alpha::fixup_Alpha_Branch);
      const MCExpr *Expr = GetFixupExpr(MO.getExpr());
      Fixups.push_back(MCFixup::create(0, Expr, Kind, true));
      return EncodeBranch(Opcode, Ra, 0);
    }
    return EncodeBranch(Opcode, Ra, getImmediate(MI, OpNo));
  };

  switch (MI.getOpcode()) {
  case Alpha::UNOP:
    return 0x2ffe0000;
  case Alpha::RDUNIQUE:
    return 0x0000009e;
  case Alpha::LDAHt:
    return EncodeMemFixup(0x09, Alpha::fixup_Alpha_TPRELHI);
  case Alpha::LDAt:
    return EncodeMemFixup(0x08, Alpha::fixup_Alpha_TPRELLO);
  case Alpha::LDQt:
    return EncodeMemFixup(0x29, Alpha::fixup_Alpha_GOTTPREL);
  case Alpha::CALL_PAL:
    return static_cast<uint32_t>(getImmediate(MI, 0)) & 0x03ffffff;
  case Alpha::MB:
    return (0x18 << 26) | 0x4000;
  case Alpha::WMB:
    return (0x18 << 26) | 0x4400;
  case Alpha::ADDLr:
  case Alpha::ADDLi:
    return EncodeOperate(0x10, 0x00);
  case Alpha::ADDQr:
  case Alpha::ADDQi:
    return EncodeOperate(0x10, 0x20);
  case Alpha::SUBLr:
  case Alpha::SUBLi:
    return EncodeOperate(0x10, 0x09);
  case Alpha::SUBQr:
  case Alpha::SUBQi:
    return EncodeOperate(0x10, 0x29);
  case Alpha::MULLr:
  case Alpha::MULLi:
    return EncodeOperate(0x13, 0x00);
  case Alpha::MULQr:
  case Alpha::MULQi:
    return EncodeOperate(0x13, 0x20);
  case Alpha::CMPULT:
  case Alpha::CMPULTi:
    return EncodeOperate(0x10, 0x1d);
  case Alpha::CMPEQ:
  case Alpha::CMPEQi:
    return EncodeOperate(0x10, 0x2d);
  case Alpha::CMPULE:
  case Alpha::CMPULEi:
    return EncodeOperate(0x10, 0x3d);
  case Alpha::CMPLT:
  case Alpha::CMPLTi:
    return EncodeOperate(0x10, 0x4d);
  case Alpha::CMPLE:
  case Alpha::CMPLEi:
    return EncodeOperate(0x10, 0x6d);
  case Alpha::SRAi:
  case Alpha::SRAr:
    return EncodeOperate(0x12, 0x3c);
  case Alpha::SLi:
  case Alpha::SLr:
    return EncodeOperate(0x12, 0x39);
  case Alpha::SRLi:
  case Alpha::SRLr:
    return EncodeOperate(0x12, 0x34);
  case Alpha::EXTBL:
  case Alpha::EXTBLi:
    return EncodeOperate(0x12, 0x06);
  case Alpha::EXTWL:
  case Alpha::EXTWLi:
    return EncodeOperate(0x12, 0x16);
  case Alpha::EXTLL:
  case Alpha::EXTLLi:
    return EncodeOperate(0x12, 0x26);
  case Alpha::S4ADDLr:
  case Alpha::S4ADDLi:
    return EncodeOperate(0x10, 0x02);
  case Alpha::S4ADDQr:
  case Alpha::S4ADDQi:
    return EncodeOperate(0x10, 0x22);
  case Alpha::S8ADDLr:
  case Alpha::S8ADDLi:
    return EncodeOperate(0x10, 0x12);
  case Alpha::S8ADDQr:
  case Alpha::S8ADDQi:
    return EncodeOperate(0x10, 0x32);
  case Alpha::S4SUBLr:
  case Alpha::S4SUBLi:
    return EncodeOperate(0x10, 0x0b);
  case Alpha::S4SUBQr:
  case Alpha::S4SUBQi:
    return EncodeOperate(0x10, 0x2b);
  case Alpha::S8SUBLr:
  case Alpha::S8SUBLi:
    return EncodeOperate(0x10, 0x1b);
  case Alpha::S8SUBQr:
  case Alpha::S8SUBQi:
    return EncodeOperate(0x10, 0x3b);
  case Alpha::UMULHr:
  case Alpha::UMULHi:
    return EncodeOperate(0x13, 0x30);
  case Alpha::SEXTB:
    return EncodeOperate2(0x1c, 0x00);
  case Alpha::SEXTW:
    return EncodeOperate2(0x1c, 0x01);
  case Alpha::ANDr:
  case Alpha::ANDi:
    return EncodeOperate(0x11, 0x00);
  case Alpha::BICr:
  case Alpha::BICi:
    return EncodeOperate(0x11, 0x08);
  case Alpha::BISr:
  case Alpha::BISi:
    return EncodeOperate(0x11, 0x20);
  case Alpha::ORNOTr:
  case Alpha::ORNOTi:
    return EncodeOperate(0x11, 0x28);
  case Alpha::CMOVEQi:
    return EncodeCmovLit(0x11, 0x24);
  case Alpha::CMOVEQr:
    return EncodeCmovReg(0x11, 0x24);
  case Alpha::CMOVNEi:
    return EncodeCmovLit(0x11, 0x26);
  case Alpha::CMOVNEr:
    return EncodeCmovReg(0x11, 0x26);
  case Alpha::CMOVLTi:
    return EncodeCmovLit(0x11, 0x44);
  case Alpha::CMOVLTr:
    return EncodeCmovReg(0x11, 0x44);
  case Alpha::CMOVLEi:
    return EncodeCmovLit(0x11, 0x64);
  case Alpha::CMOVLEr:
    return EncodeCmovReg(0x11, 0x64);
  case Alpha::CMOVGTi:
    return EncodeCmovLit(0x11, 0x66);
  case Alpha::CMOVGTr:
    return EncodeCmovReg(0x11, 0x66);
  case Alpha::CMOVGEi:
    return EncodeCmovLit(0x11, 0x46);
  case Alpha::CMOVGEr:
    return EncodeCmovReg(0x11, 0x46);
  case Alpha::CMOVLBCi:
    return EncodeCmovLit(0x11, 0x16);
  case Alpha::CMOVLBCr:
    return EncodeCmovReg(0x11, 0x16);
  case Alpha::CMOVLBSi:
    return EncodeCmovLit(0x11, 0x14);
  case Alpha::CMOVLBSr:
    return EncodeCmovReg(0x11, 0x14);
  case Alpha::XORr:
  case Alpha::XORi:
    return EncodeOperate(0x11, 0x40);
  case Alpha::EQVr:
  case Alpha::EQVi:
    return EncodeOperate(0x11, 0x48);
  case Alpha::ZAPNOTi:
    return EncodeOperate(0x12, 0x31);
  case Alpha::BEQ:
    return EncodeBranchOperand(0x39, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BNE:
    return EncodeBranchOperand(0x3d, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BGE:
    return EncodeBranchOperand(0x3e, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BGT:
    return EncodeBranchOperand(0x3f, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BLE:
    return EncodeBranchOperand(0x3b, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BLT:
    return EncodeBranchOperand(0x3a, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BLBC:
    return EncodeBranchOperand(0x38, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BLBS:
    return EncodeBranchOperand(0x3c, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::BR:
    return EncodeBranchOperand(0x30, 31, 0);
  case Alpha::BSR:
    return EncodeBranchOperand(0x34, 26, 0);
  case Alpha::FBEQ:
    return EncodeBranchOperand(0x31, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::FBLT:
    return EncodeBranchOperand(0x32, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::FBLE:
    return EncodeBranchOperand(0x33, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::FBNE:
    return EncodeBranchOperand(0x35, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::FBGE:
    return EncodeBranchOperand(0x36, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::FBGT:
    return EncodeBranchOperand(0x37, getRegisterNumber(MI.getOperand(0)), 1);
  case Alpha::JSR:
  case Alpha::JSRl:
    return (0x1a << 26) | (26 << 21) | (27 << 16) | (1 << 14);
  case Alpha::JSRs:
    return (0x1a << 26) | (26 << 21) | (27 << 16) | (1 << 14);
  case Alpha::JMP:
    return (0x1a << 26) | (31 << 21) |
           (getRegisterNumber(MI.getOperand(0)) << 16);
  case Alpha::LDA:
    return EncodeMem(0x08);
  case Alpha::LDAHg: {
    unsigned Ra = getRegisterNumber(MI.getOperand(0));
    unsigned Rb = getRegisterNumber(MI.getOperand(2));
    const MCExpr *Distance = MCConstantExpr::create(4, Ctx);
    Fixups.push_back(MCFixup::create(0, Distance, Alpha::fixup_Alpha_GPDISP));
    return (0x09 << 26) | (Ra << 21) | (Rb << 16);
  }
  case Alpha::LDAg:
    return (0x08 << 26) | (getRegisterNumber(MI.getOperand(0)) << 21) |
           (getRegisterNumber(MI.getOperand(2)) << 16);
  case Alpha::LDAHr:
    return EncodeMemFixup(0x09, Alpha::fixup_Alpha_REFHI);
  case Alpha::LDAH:
    return EncodeMem(0x09);
  case Alpha::LDBU:
    return EncodeMem(0x0a);
  case Alpha::LDBUr:
    return EncodeMemFixup(0x0a, Alpha::fixup_Alpha_REFLO);
  case Alpha::LDL:
    return EncodeMem(0x28);
  case Alpha::LDLr:
    return EncodeMemFixup(0x28, Alpha::fixup_Alpha_REFLO);
  case Alpha::LDQ:
    return EncodeMem(0x29);
  case Alpha::LDQ_U:
    return EncodeMem(0x0b);
  case Alpha::LDQr:
    return EncodeMemFixup(0x29, Alpha::fixup_Alpha_REFLO);
  case Alpha::LDQl:
    return EncodeMemFixup(0x29, Ctx.getTargetTriple().isOSBinFormatELF()
                                    ? Alpha::fixup_Alpha_LITERAL
                                    : Alpha::fixup_Alpha_REFLO);
  case Alpha::LDAr:
    return EncodeMemFixup(0x08, Alpha::fixup_Alpha_REFLO);
  case Alpha::LDWU:
    return EncodeMem(0x0c);
  case Alpha::LDWUr:
    return EncodeMemFixup(0x0c, Alpha::fixup_Alpha_REFLO);
  case Alpha::STB:
    return EncodeMem(0x0e);
  case Alpha::STBr:
    return EncodeMemFixup(0x0e, Alpha::fixup_Alpha_REFLO);
  case Alpha::STW:
    return EncodeMem(0x0d);
  case Alpha::STWr:
    return EncodeMemFixup(0x0d, Alpha::fixup_Alpha_REFLO);
  case Alpha::STL:
    return EncodeMem(0x2c);
  case Alpha::STLr:
    return EncodeMemFixup(0x2c, Alpha::fixup_Alpha_REFLO);
  case Alpha::STQ:
    return EncodeMem(0x2d);
  case Alpha::STQr:
    return EncodeMemFixup(0x2d, Alpha::fixup_Alpha_REFLO);
  case Alpha::LDS:
    return EncodeMem(0x22);
  case Alpha::LDSr:
    return EncodeMemFixup(0x22, Alpha::fixup_Alpha_REFLO);
  case Alpha::LDT:
    return EncodeMem(0x23);
  case Alpha::LDTr:
    return EncodeMemFixup(0x23, Alpha::fixup_Alpha_REFLO);
  case Alpha::STS:
    return EncodeMem(0x26);
  case Alpha::STSr:
    return EncodeMemFixup(0x26, Alpha::fixup_Alpha_REFLO);
  case Alpha::STT:
    return EncodeMem(0x27);
  case Alpha::STTr:
    return EncodeMemFixup(0x27, Alpha::fixup_Alpha_REFLO);
  case Alpha::ITOFS:
    return EncodeFP(0x14, 0x004, getRegisterNumber(MI.getOperand(1)), 31,
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::ITOFT:
    return EncodeFP(0x14, 0x024, getRegisterNumber(MI.getOperand(1)), 31,
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FTOIS:
    return EncodeFP(0x1c, 0x078, getRegisterNumber(MI.getOperand(1)), 31,
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FTOIT:
    return EncodeFP(0x1c, 0x070, getRegisterNumber(MI.getOperand(1)), 31,
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CVTQS:
    return EncodeFP(0x16, 0x7bc, 31, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CVTQT:
    return EncodeFP(0x16, 0x7be, 31, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CVTST:
    return EncodeFP(0x16, 0x6ac, 31, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CVTTS:
    return EncodeFP(0x16, 0x7ac, 31, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CVTTQ:
    return EncodeFP(0x16, 0x52f, 31, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CMPTEQ:
    return EncodeFP(0x16, 0x5a5, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CMPTLE:
    return EncodeFP(0x16, 0x5a7, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CMPTLT:
    return EncodeFP(0x16, 0x5a6, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CMPTUN:
    return EncodeFP(0x16, 0x5a4, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::SQRTS:
    return EncodeFP(0x14, 0x58b, 31, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::SQRTT:
    return EncodeFP(0x14, 0x5ab, 31, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::DIVS:
    return EncodeFP(0x16, 0x583, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::DIVT:
    return EncodeFP(0x16, 0x5a3, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::ADDS:
    return EncodeFP(0x16, 0x580, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::ADDT:
    return EncodeFP(0x16, 0x5a0, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::SUBS:
    return EncodeFP(0x16, 0x581, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::SUBT:
    return EncodeFP(0x16, 0x5a1, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::MULS:
    return EncodeFP(0x16, 0x582, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::MULT:
    return EncodeFP(0x16, 0x5a2, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CPYSES:
  case Alpha::CPYSESt:
  case Alpha::CPYSET:
    return EncodeFP(0x17, 0x022, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CPYSNS:
  case Alpha::CPYSNSt:
  case Alpha::CPYSNTs:
    return EncodeFP(0x17, 0x021, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CPYSNT:
    return EncodeFP(0x17, 0x021, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CPYSS:
  case Alpha::CPYSSt:
  case Alpha::CPYSTs:
    return EncodeFP(0x17, 0x020, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::CPYST:
    return EncodeFP(0x17, 0x020, getRegisterNumber(MI.getOperand(1)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FCMOVEQS:
  case Alpha::FCMOVEQT:
    return EncodeFP(0x17, 0x02a, getRegisterNumber(MI.getOperand(3)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FCMOVGES:
  case Alpha::FCMOVGET:
    return EncodeFP(0x17, 0x02d, getRegisterNumber(MI.getOperand(3)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FCMOVGTS:
  case Alpha::FCMOVGTT:
    return EncodeFP(0x17, 0x02f, getRegisterNumber(MI.getOperand(3)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FCMOVLES:
  case Alpha::FCMOVLET:
    return EncodeFP(0x17, 0x02e, getRegisterNumber(MI.getOperand(3)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FCMOVLTS:
  case Alpha::FCMOVLTT:
    return EncodeFP(0x17, 0x02c, getRegisterNumber(MI.getOperand(3)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::FCMOVNES:
  case Alpha::FCMOVNET:
    return EncodeFP(0x17, 0x02b, getRegisterNumber(MI.getOperand(3)),
                    getRegisterNumber(MI.getOperand(2)),
                    getRegisterNumber(MI.getOperand(0)));
  case Alpha::RETDAG:
  case Alpha::RETDAGp:
    return (0x1a << 26) | (31 << 21) | (26 << 16) | (2 << 14) | 1;
  default:
    Ctx.reportError(MI.getLoc(), "Alpha instruction encoding is not implemented for opcode " + Twine(MI.getOpcode()));
    return 0;
  }
}

void AlphaMCCodeEmitter::encodeInstruction(
    const MCInst &MI, SmallVectorImpl<char> &CB,
    SmallVectorImpl<MCFixup> &Fixups, const MCSubtargetInfo &STI) const {
  support::endian::write(CB, getBinaryCodeForInstr(MI, Fixups),
                         endianness::little);
}

MCCodeEmitter *llvm::createAlphaMCCodeEmitter(const MCInstrInfo &MCII,
                                              MCContext &Ctx) {
  return new AlphaMCCodeEmitter(MCII, Ctx);
}
