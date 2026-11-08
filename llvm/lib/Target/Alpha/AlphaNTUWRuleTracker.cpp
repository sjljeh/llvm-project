//===- AlphaNTUWRuleTracker.cpp - Alpha NTUW instruction semantics --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Describes the Alpha instructions that Windows prologues and epilogues use
// to the target-independent NTUW rule tracker.
//
//===----------------------------------------------------------------------===//

#include "Alpha.h"
#include "MCTargetDesc/AlphaMCTargetDesc.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/NTUWRuleTracker.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {

class AlphaNTUWRuleTracker final : public NTUWRuleTracker {
  const MCRegisterInfo &MRI;

public:
  explicit AlphaNTUWRuleTracker(AsmPrinter &AP)
      : NTUWRuleTracker(AP, NTRISCWinEH::Machine::Alpha, /*FramePointer=*/15),
        MRI(AP.TM.getMCRegisterInfo()) {}

  /// The Alpha register definitions carry no hardware encoding; the number
  /// is in the name (R0-R31, F0-F31).
  std::optional<unsigned> numberOf(unsigned R) const {
    StringRef Name = MRI.getName(R);
    unsigned Num;
    if (Name.size() < 2 || Name.drop_front().getAsInteger(10, Num) || Num > 31)
      return std::nullopt;
    return Num;
  }

  std::optional<Reg> mapRegister(unsigned R) const override {
    std::optional<unsigned> Num = numberOf(R);
    if (!Num)
      return std::nullopt;
    if (MRI.getRegClass(Alpha::GPRCRegClassID).contains(R))
      return Reg{RegClass::GPR, uint8_t(*Num)};
    if (MRI.getRegClass(Alpha::F8RCRegClassID).contains(R) ||
        MRI.getRegClass(Alpha::F4RCRegClassID).contains(R))
      return Reg{RegClass::FPR, uint8_t(*Num)};
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
    auto Push = [&](Effect::Kind K, Reg Dst, std::optional<Reg> Src,
                    std::optional<Reg> Src2, int64_t Value, uint8_t Size = 0) {
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
    auto IsZero = [](std::optional<Reg> X) {
      return X && X->Class == RegClass::GPR && X->Num == 31;
    };

    unsigned Op = I.getOpcode();
    switch (Op) {
    case Alpha::LDA:
    case Alpha::LDAH: {
      // [ra, disp, rb]
      auto Dst = R(0), Base = R(2);
      auto Disp = Imm(1);
      if (!Dst || !Base)
        return false;
      if (!Disp) {
        Push(Effect::Clobber, *Dst, std::nullopt, std::nullopt, 0);
        break;
      }
      int64_t Value = SignExtend64<16>(*Disp);
      if (Op == Alpha::LDAH)
        Value *= 65536;
      Push(Effect::AddImm, *Dst, Base, std::nullopt, Value);
      break;
    }
    case Alpha::STQ:
    case Alpha::STL:
    case Alpha::STT:
    case Alpha::STS: {
      // [ra, disp, rb]
      auto Src = R(0), Base = R(2);
      auto Disp = Imm(1);
      if (!Src || !Base)
        return false;
      if (!Disp)
        break;
      Effect E;
      E.K = Effect::Store;
      E.Src = *Src;
      E.Src2 = *Base;
      E.Imm = SignExtend64<16>(*Disp);
      E.Size = (Op == Alpha::STQ || Op == Alpha::STT) ? 8 : 4;
      D.Effects.push_back(E);
      break;
    }
    case Alpha::LDQ:
    case Alpha::LDL:
    case Alpha::LDT:
    case Alpha::LDS: {
      auto Dst = R(0), Base = R(2);
      auto Disp = Imm(1);
      if (!Dst || !Base)
        return false;
      if (Disp)
        Push(Effect::Load, *Dst, Base, std::nullopt, SignExtend64<16>(*Disp),
             (Op == Alpha::LDQ || Op == Alpha::LDT) ? 8 : 4);
      else
        Push(Effect::Clobber, *Dst, std::nullopt, std::nullopt, 0);
      break;
    }
    case Alpha::BISr: {
      // [rc, ra, rb]
      auto Dst = R(0), A = R(1), B = R(2);
      if (!Dst || !A || !B)
        return false;
      if (*A == *B || IsZero(B))
        Push(Effect::Copy, *Dst, A, std::nullopt, 0);
      else if (IsZero(A))
        Push(Effect::Copy, *Dst, B, std::nullopt, 0);
      else
        Push(Effect::Clobber, *Dst, std::nullopt, std::nullopt, 0);
      break;
    }
    case Alpha::BISi:
    case Alpha::ADDQi:
    case Alpha::SUBQi: {
      // [rc, ra, lit]
      auto Dst = R(0), A = R(1);
      auto Lit = Imm(2);
      if (!Dst || !A)
        return false;
      if (!Lit)
        Push(Effect::Clobber, *Dst, std::nullopt, std::nullopt, 0);
      else if (Op == Alpha::BISi)
        Push(Effect::OrImm, *Dst, A, std::nullopt, *Lit & 0xff);
      else
        Push(Effect::AddImm, *Dst, A, std::nullopt,
             Op == Alpha::ADDQi ? (*Lit & 0xff) : -(*Lit & 0xff));
      break;
    }
    case Alpha::ADDQr:
    case Alpha::SUBQr: {
      auto Dst = R(0), A = R(1), B = R(2);
      if (!Dst || !A || !B)
        return false;
      Push(Op == Alpha::ADDQr ? Effect::AddReg : Effect::SubReg, *Dst, A, B, 0);
      break;
    }
    case Alpha::RETDAG:
    case Alpha::RETDAGp:
      D.IsBranch = true;
      D.LeavesFunction = true;
      break;
    case Alpha::JMP:
      // A jmp in an epilogue tail-calls; one in the body (a jump table)
      // never changes the rules.
      D.IsBranch = true;
      D.LeavesFunction = true;
      break;
    case Alpha::BR: {
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
llvm::createAlphaNTUWRuleTracker(AsmPrinter &AP) {
  return std::make_unique<AlphaNTUWRuleTracker>(AP);
}
