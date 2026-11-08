//===- PPCNTUWRuleTracker.cpp - PowerPC NTUW instruction semantics --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Describes the PowerPC instructions that Windows prologues and epilogues use
// to the target-independent NTUW rule tracker.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCMCTargetDesc.h"
#include "PPC.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/NTUWRuleTracker.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

class PPCNTUWRuleTracker final : public NTUWRuleTracker {
  const MCRegisterInfo &MRI;

public:
  explicit PPCNTUWRuleTracker(AsmPrinter &AP)
      : NTUWRuleTracker(AP, NTRISCWinEH::Machine::PPC, /*FramePointer=*/31),
        MRI(AP.TM.getMCRegisterInfo()) {}

  std::optional<Reg> mapRegister(unsigned R) const override {
    if (R == PPC::ZERO)
      return std::nullopt;
    if (MRI.getRegClass(PPC::GPRCRegClassID).contains(R))
      return Reg{RegClass::GPR, uint8_t(MRI.getEncodingValue(R))};
    if (MRI.getRegClass(PPC::F8RCRegClassID).contains(R) ||
        MRI.getRegClass(PPC::F4RCRegClassID).contains(R))
      return Reg{RegClass::FPR, uint8_t(MRI.getEncodingValue(R))};
    if (R == PPC::LR)
      return Reg{RegClass::SPR, NTRISCWinEH::SPR_LR};
    if (R == PPC::CTR)
      return Reg{RegClass::SPR, NTRISCWinEH::SPR_CTR};
    // Only CR2-CR4 are nonvolatile; changes to the other fields need no
    // rule.
    for (MCRegister Field : {PPC::CR2, PPC::CR3, PPC::CR4})
      if (MRI.isSubRegisterEq(Field, R))
        return Reg{RegClass::SPR, NTRISCWinEH::SPR_CR};
    return std::nullopt;
  }

  bool decode(const MCInst &I, Decoded &D) const override {
    auto IsZero = [&](unsigned N) {
      return N < I.getNumOperands() && I.getOperand(N).isReg() &&
             I.getOperand(N).getReg() == PPC::ZERO;
    };
    auto R = [&](unsigned N) -> std::optional<Reg> {
      if (N >= I.getNumOperands() || !I.getOperand(N).isReg())
        return std::nullopt;
      return mapRegister(I.getOperand(N).getReg());
    };
    auto Imm = [&](unsigned N) -> std::optional<int64_t> {
      if (N >= I.getNumOperands() || !I.getOperand(N).isImm())
        return std::nullopt;
      return I.getOperand(N).getImm();
    };
    auto Push = [&](Effect::Kind K, Reg Dst, std::optional<Reg> Src = {},
                    std::optional<Reg> Src2 = {}, int64_t Value = 0,
                    uint8_t Size = 0) {
      Effect E;
      E.K = K;
      E.Dst = Dst;
      if (Src)
        E.Src = *Src;
      if (Src2)
        E.Src2 = *Src2;
      E.Imm = Value;
      E.Size = Size;
      D.Effects.push_back(E);
    };
    const Reg LR{RegClass::SPR, NTRISCWinEH::SPR_LR};
    const Reg CR{RegClass::SPR, NTRISCWinEH::SPR_CR};

    unsigned Op = I.getOpcode();
    switch (Op) {
    case PPC::STW:
    case PPC::STFD:
    case PPC::STWU: {
      // stw/stfd: [rs, d, ra]; stwu: [ea_res, rs, d, ra].
      unsigned Base = Op == PPC::STWU ? 1 : 0;
      auto Src = R(Base), Ra = R(Base + 2);
      auto Disp = Imm(Base + 1);
      if (!Src || IsZero(Base + 2))
        return false;
      if (!Ra)
        return false;
      if (!Disp) {
        if (Op == PPC::STWU)
          Push(Effect::Clobber, *Ra);
        break;
      }
      Effect E;
      E.K = Effect::Store;
      E.Src = *Src;
      E.Src2 = *Ra;
      E.Imm = *Disp;
      E.Size = Op == PPC::STFD ? 8 : 4;
      D.Effects.push_back(E);
      if (Op == PPC::STWU)
        Push(Effect::AddImm, *Ra, Ra, std::nullopt, *Disp);
      break;
    }
    case PPC::STWUX: {
      // [ea_res, rs, ra, rb]
      auto Src = R(1), Ra = R(2), Rb = R(3);
      if (!Src || !Ra || !Rb)
        return false;
      Effect E;
      E.K = Effect::StoreIndexed;
      E.Src = *Src;
      E.Src2 = *Ra;
      E.Src3 = *Rb;
      E.Size = 4;
      D.Effects.push_back(E);
      Push(Effect::AddReg, *Ra, Ra, Rb);
      break;
    }
    case PPC::LWZ:
    case PPC::LFD: {
      // [rt, d, ra]
      auto Dst = R(0), Ra = R(2);
      auto Disp = Imm(1);
      if (!Dst)
        return false;
      if (Ra && Disp && Op == PPC::LWZ && *Disp == 0 &&
          Ra->Class == RegClass::GPR && Ra->Num == 1)
        Push(Effect::LoadBackChain, *Dst, Ra, std::nullopt, 0, 4);
      else if (Ra && Disp)
        Push(Effect::Load, *Dst, Ra, std::nullopt, *Disp,
             Op == PPC::LFD ? 8 : 4);
      else
        Push(Effect::Clobber, *Dst);
      break;
    }
    case PPC::ADDI:
    case PPC::ADDIS: {
      // [rt, ra, imm]
      auto Dst = R(0), Ra = R(1);
      auto Value = Imm(2);
      if (!Dst)
        return false;
      if (!Value) {
        Push(Effect::Clobber, *Dst);
        break;
      }
      int64_t V = SignExtend64<16>(*Value);
      if (Op == PPC::ADDIS)
        V *= 65536;
      if (IsZero(1))
        Push(Effect::SetConst, *Dst, std::nullopt, std::nullopt, V);
      else if (Ra)
        Push(Effect::AddImm, *Dst, Ra, std::nullopt, V);
      else
        return false;
      break;
    }
    case PPC::LI:
    case PPC::LIS: {
      auto Dst = R(0);
      auto Value = Imm(1);
      if (!Dst)
        return false;
      if (!Value) {
        Push(Effect::Clobber, *Dst);
        break;
      }
      int64_t V = SignExtend64<16>(*Value);
      if (Op == PPC::LIS)
        V *= 65536;
      Push(Effect::SetConst, *Dst, std::nullopt, std::nullopt, V);
      break;
    }
    case PPC::ORI:
    case PPC::ORIS: {
      // [ra, rs, imm]
      auto Dst = R(0), Src = R(1);
      auto Value = Imm(2);
      if (!Dst || !Src)
        return false;
      if (!Value) {
        Push(Effect::Clobber, *Dst);
        break;
      }
      int64_t V = *Value & 0xffff;
      if (Op == PPC::ORIS)
        V <<= 16;
      Push(Effect::OrImm, *Dst, Src, std::nullopt, V);
      break;
    }
    case PPC::OR: {
      // [ra, rs, rb]; mr ra, rs is `or ra, rs, rs`.
      auto Dst = R(0), A = R(1), B = R(2);
      if (!Dst || !A || !B)
        return false;
      if (*A == *B)
        Push(Effect::Copy, *Dst, A);
      else
        Push(Effect::Clobber, *Dst);
      break;
    }
    case PPC::ADD4:
    case PPC::SUBF: {
      // add [rt, ra, rb] = ra + rb; subf [rt, ra, rb] = rb - ra.
      auto Dst = R(0), A = R(1), B = R(2);
      if (!Dst || !A || !B)
        return false;
      if (Op == PPC::ADD4)
        Push(Effect::AddReg, *Dst, A, B);
      else
        Push(Effect::SubReg, *Dst, B, A);
      break;
    }
    case PPC::MFLR: {
      auto Dst = R(0);
      if (!Dst)
        return false;
      Push(Effect::Copy, *Dst, LR);
      break;
    }
    case PPC::MTLR: {
      auto Src = R(0);
      if (!Src)
        return false;
      Push(Effect::Copy, LR, Src);
      break;
    }
    case PPC::MFCR: {
      auto Dst = R(0);
      if (!Dst)
        return false;
      Push(Effect::Copy, *Dst, CR);
      break;
    }
    case PPC::MTCRF: {
      // [mask, rs]. The nonvolatile fields CR2-CR4 are mask bits 0x38.
      auto Mask = Imm(0);
      auto Src = R(1);
      if (!Mask || !Src)
        return false;
      unsigned Nonvolatile = *Mask & 0x38;
      if (Nonvolatile == 0x38)
        Push(Effect::Copy, CR, Src);
      else if (Nonvolatile)
        Push(Effect::Clobber, CR);
      break;
    }
    case PPC::BLR:
      D.IsBranch = true;
      D.LeavesFunction = true;
      break;
    case PPC::BCTR:
      // A bctr in an epilogue tail-calls; in the body (a jump table) it never
      // changes the rules.
      D.IsBranch = true;
      D.LeavesFunction = true;
      break;
    case PPC::B: {
      D.IsBranch = true;
      if (I.getNumOperands() && I.getOperand(0).isExpr())
        if (const auto *Ref =
                dyn_cast<MCSymbolRefExpr>(I.getOperand(0).getExpr()))
          D.LeavesFunction = !Ref->getSymbol().isTemporary();
      break;
    }
    default:
      return false;
    }
    D.Known = true;
    return true;
  }
};

} // namespace

std::unique_ptr<AsmPrinterHandler>
llvm::createPPCNTUWRuleTracker(AsmPrinter &AP) {
  return std::make_unique<PPCNTUWRuleTracker>(AP);
}
