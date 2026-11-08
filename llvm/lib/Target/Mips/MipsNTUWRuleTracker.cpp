//===- MipsNTUWRuleTracker.cpp - MIPS NTUW instruction semantics ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Describes the MIPS instructions that Windows prologues and epilogues use to
// the target-independent NTUW rule tracker.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "Mips.h"
#include "MipsRegisterInfo.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/NTUWRuleTracker.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

class MipsNTUWRuleTracker final : public NTUWRuleTracker {
  const MCRegisterInfo &MRI;

public:
  explicit MipsNTUWRuleTracker(AsmPrinter &AP)
      : NTUWRuleTracker(AP, NTRISCWinEH::Machine::MIPS, /*FramePointer=*/30),
        MRI(AP.TM.getMCRegisterInfo()) {}

  std::optional<Reg> mapRegister(unsigned R) const override {
    if (Mips::GPR32RegClass.contains(R))
      return Reg{RegClass::GPR, uint8_t(MRI.getEncodingValue(R))};
    // With FR=0, a double occupies an even/odd pair named by the even half.
    if (Mips::FGR32RegClass.contains(R) || Mips::AFGR64RegClass.contains(R))
      return Reg{RegClass::FPR, uint8_t(MRI.getEncodingValue(R) & ~1u)};
    return std::nullopt;
  }

  bool decode(const MCInst &I, Decoded &D) const override {
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
    auto Add = [&](Effect::Kind K, std::optional<Reg> Dst,
                   std::optional<Reg> Src, std::optional<Reg> Src2,
                   int64_t Value, uint8_t Size = 0) {
      Effect E;
      E.K = K;
      E.Dst = *Dst;
      if (Src)
        E.Src = *Src;
      if (Src2)
        E.Src2 = *Src2;
      E.Imm = Value;
      E.Size = Size;
      D.Effects.push_back(E);
    };

    switch (I.getOpcode()) {
    case Mips::ADDiu: {
      auto Dst = R(0), Src = R(1);
      auto Value = Imm(2);
      if (!Dst || !Src)
        return false;
      if (Value)
        Add(Effect::AddImm, Dst, Src, std::nullopt, *Value);
      else
        Add(Effect::Clobber, Dst, std::nullopt, std::nullopt, 0);
      break;
    }
    case Mips::ORi: {
      auto Dst = R(0), Src = R(1);
      auto Value = Imm(2);
      if (!Dst || !Src)
        return false;
      if (Value)
        Add(Effect::OrImm, Dst, Src, std::nullopt, *Value & 0xffff);
      else
        Add(Effect::Clobber, Dst, std::nullopt, std::nullopt, 0);
      break;
    }
    case Mips::LUi: {
      auto Dst = R(0);
      auto Value = Imm(1);
      if (!Dst)
        return false;
      if (Value)
        Add(Effect::SetConst, Dst, std::nullopt, std::nullopt,
            SignExtend64<32>(uint64_t(*Value & 0xffff) << 16));
      else
        Add(Effect::Clobber, Dst, std::nullopt, std::nullopt, 0);
      break;
    }
    case Mips::ADDu:
    case Mips::SUBu: {
      auto Dst = R(0), A = R(1), B = R(2);
      if (!Dst || !A || !B)
        return false;
      Add(I.getOpcode() == Mips::ADDu ? Effect::AddReg : Effect::SubReg, Dst, A,
          B, 0);
      break;
    }
    case Mips::OR: {
      auto Dst = R(0), A = R(1), B = R(2);
      if (!Dst || !A || !B)
        return false;
      // move rd, rs is `or rd, rs, $zero`.
      if (B->Class == RegClass::GPR && B->Num == 0)
        Add(Effect::Copy, Dst, A, std::nullopt, 0);
      else if (A->Class == RegClass::GPR && A->Num == 0)
        Add(Effect::Copy, Dst, B, std::nullopt, 0);
      else if (*A == *B)
        Add(Effect::Copy, Dst, A, std::nullopt, 0);
      else
        Add(Effect::Clobber, Dst, std::nullopt, std::nullopt, 0);
      break;
    }
    case Mips::SW:
    case Mips::SDC1:
    case Mips::SWC1: {
      auto Src = R(0), Base = R(1);
      auto Offset = Imm(2);
      if (!Src || !Base)
        return false;
      // A store with a relocated offset is not a frame store.
      if (!Offset)
        break;
      Effect E;
      E.K = Effect::Store;
      E.Src = *Src;
      E.Src2 = *Base;
      E.Imm = *Offset;
      E.Size = I.getOpcode() == Mips::SDC1 ? 8 : 4;
      // swc1 stores half of a pair; it never holds a whole saved register.
      if (I.getOpcode() == Mips::SWC1)
        E.Size = 4;
      D.Effects.push_back(E);
      break;
    }
    case Mips::LW:
    case Mips::LDC1:
    case Mips::LWC1: {
      auto Dst = R(0), Base = R(1);
      auto Offset = Imm(2);
      if (!Dst || !Base)
        return false;
      if (Offset)
        Add(Effect::Load, Dst, Base, std::nullopt, *Offset,
            I.getOpcode() == Mips::LDC1 ? 8 : 4);
      else
        Add(Effect::Clobber, Dst, std::nullopt, std::nullopt, 0);
      break;
    }
    case Mips::JR: {
      // `jr $ra` returns; any other jr in an epilogue is a tail call. A jr in
      // the body (a jump table) never changes the rules, so it does not
      // matter there.
      D.IsBranch = true;
      D.LeavesFunction = true;
      break;
    }
    case Mips::J: {
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
llvm::createMipsNTUWRuleTracker(AsmPrinter &AP) {
  return std::make_unique<MipsNTUWRuleTracker>(AP);
}
