//===- RISCVWinEHTest.cpp - RISC-V64 RVUW decoder and evaluator tests -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/RISCVWinEH.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"
#include <vector>

using namespace llvm;
using namespace llvm::RISCVWinEH;

namespace {

using Slots = std::vector<std::pair<uint32_t, uint32_t>>;

/// Appends one rule, selecting the two-slot form like the encoder does.
void addRule(Slots &Program, uint32_t CodeOffset, unsigned Opcode,
             unsigned Register, int32_t Operand, int32_t Scale = 0) {
  if (isScaledOpcode(Opcode)) {
    Program.push_back(
        {CodeOffset, Opcode | (Register << RegisterShift) |
                         ((uint32_t(Scale) & OperandMask) << OperandShift)});
    Program.push_back({uint32_t(Operand), 0});
    return;
  }
  if (Operand < SmallOperandMin || Operand > SmallOperandMax) {
    Program.push_back(
        {CodeOffset, getLargeOpcode(Opcode) | (Register << RegisterShift)});
    Program.push_back({uint32_t(Operand), 0});
    return;
  }
  Program.push_back(
      {CodeOffset, Opcode | (Register << RegisterShift) |
                       ((uint32_t(Operand) & OperandMask) << OperandShift)});
}

struct Record {
  uint16_t HeaderSize = UnwindHeaderSize;
  uint32_t Flags = 0;
  uint32_t PrologEnd = 0;
  int32_t EstablisherFrameOffset = 0;
  uint16_t RequiredState = STATE_Integer;
  uint8_t EstablisherRegister = 0;
  uint8_t Reserved = 0;
  Slots Prolog;
  struct Scope {
    uint32_t Start, End;
    Slots Codes;
  };
  std::vector<Scope> Epilogs;

  std::vector<uint8_t> build() const {
    std::vector<uint8_t> Bytes;
    auto Put32 = [&](uint32_t V) {
      for (int I = 0; I != 4; ++I)
        Bytes.push_back(uint8_t(V >> (8 * I)));
    };
    auto Put16 = [&](uint16_t V) {
      Bytes.push_back(uint8_t(V));
      Bytes.push_back(uint8_t(V >> 8));
    };
    uint32_t EpilogSlots = 0;
    for (const Scope &S : Epilogs)
      EpilogSlots += S.Codes.size();
    uint32_t Size = UnwindHeaderSize + Prolog.size() * UnwindCodeSize +
                    Epilogs.size() * EpilogScopeSize +
                    EpilogSlots * UnwindCodeSize;
    Put32(UnwindMagic);
    Put16(HeaderSize);
    Bytes.push_back(EstablisherRegister);
    Bytes.push_back(Reserved);
    Put32(Size);
    Put32(Flags);
    Put32(PrologEnd);
    Put32(uint32_t(EstablisherFrameOffset));
    Put16(Prolog.size());
    Put16(Epilogs.size());
    Put16(EpilogSlots);
    Put16(RequiredState);
    for (auto [Offset, OpInfo] : Prolog) {
      Put32(Offset);
      Put32(OpInfo);
    }
    uint32_t First = 0;
    for (const Scope &S : Epilogs) {
      Put32(S.Start);
      Put32(S.End);
      Put16(First);
      Put16(S.Codes.size());
      Put32(0);
      First += S.Codes.size();
    }
    for (const Scope &S : Epilogs)
      for (auto [Offset, OpInfo] : S.Codes) {
        Put32(Offset);
        Put32(OpInfo);
      }
    return Bytes;
  }
};

/// A frame with an `addi sp` / `sd ra` prologue and a matching epilogue.
Record makeBasicFrame() {
  Record R;
  R.PrologEnd = 8;
  R.EstablisherFrameOffset = -32;
  addRule(R.Prolog, 4, UOP_SetCFA, 2, 32);
  addRule(R.Prolog, 8, UOP_SaveGPR, 1, -8);
  Record::Scope Epilog{20, 30, {}};
  addRule(Epilog.Codes, 4, UOP_SameGPR, 1, 0);
  addRule(Epilog.Codes, 8, UOP_SetCFA, 2, 0);
  R.Epilogs.push_back(Epilog);
  return R;
}

TEST(RISCVWinEHTest, EvaluatesPrologueBodyAndEpilogue) {
  std::vector<uint8_t> Bytes = makeBasicFrame().build();
  Expected<DecodedUnwindInfo> Info = decodeUnwindInfo(Bytes, 30);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  EXPECT_EQ(Info->RequiredState, STATE_Integer);

  // Before the allocation.
  UnwindState S = evaluateState(*Info, 0);
  EXPECT_EQ(S.CFARegister, 2);
  EXPECT_EQ(S.CFAOffset, 0);
  EXPECT_EQ(S.GPR[1].Kind, RuleKind::Unchanged);

  // After `addi sp`, before `sd ra`.
  S = evaluateState(*Info, 4);
  EXPECT_EQ(S.CFAOffset, 32);
  EXPECT_EQ(S.GPR[1].Kind, RuleKind::Unchanged);

  // Body.
  S = evaluateState(*Info, 12);
  EXPECT_EQ(S.CFAOffset, 32);
  EXPECT_EQ(S.GPR[1].Kind, RuleKind::Saved);
  EXPECT_EQ(S.GPR[1].Offset, -8);
  EXPECT_FALSE(S.hasExactCallerPC());

  // Epilogue: ra restored, then the frame released.
  S = evaluateState(*Info, 20);
  EXPECT_EQ(S.GPR[1].Kind, RuleKind::Saved);
  S = evaluateState(*Info, 24);
  EXPECT_EQ(S.GPR[1].Kind, RuleKind::Unchanged);
  EXPECT_EQ(S.CFAOffset, 32);
  S = evaluateState(*Info, 28);
  EXPECT_EQ(S.CFAOffset, 0);

  // Past the epilogue scope the body state holds again.
  S = evaluateState(*Info, 16);
  EXPECT_EQ(S.GPR[1].Kind, RuleKind::Saved);
}

TEST(RISCVWinEHTest, DecodesTwoSlotRules) {
  Record R;
  R.PrologEnd = 12;
  R.RequiredState = STATE_Integer | STATE_Vector;
  addRule(R.Prolog, 4, UOP_SetCFA, 2, 1 << 22);
  addRule(R.Prolog, 8, UOP_SetCFAScaled, 2, 48, 3);
  addRule(R.Prolog, 12, UOP_SaveVR, 8, -16, -2);
  std::vector<uint8_t> Bytes = R.build();
  Expected<DecodedUnwindInfo> Info = decodeUnwindInfo(Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  ASSERT_EQ(Info->PrologCodes.size(), 3u);
  EXPECT_EQ(Info->PrologCodes[0].Opcode, UOP_SetCFALarge);
  EXPECT_EQ(Info->PrologCodes[0].Operand, 1 << 22);
  EXPECT_EQ(Info->PrologCodes[0].SlotCount, 2);
  EXPECT_EQ(Info->PrologCodes[1].Operand, 48);
  EXPECT_EQ(Info->PrologCodes[1].Scale, 3);

  UnwindState S = evaluateState(*Info, 12);
  EXPECT_EQ(S.CFAOffset, 48);
  EXPECT_EQ(S.CFAScale, 3);
  EXPECT_EQ(S.VR[8].Kind, RuleKind::Saved);
  EXPECT_EQ(S.VR[8].Offset, -16);
  EXPECT_EQ(S.VR[8].Scale, -2);

  // Vector rules need the vector state bit.
  R.RequiredState = STATE_Integer;
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Bytes), Failed());
}

TEST(RISCVWinEHTest, FloatingPointRulesNeedFloatingState) {
  Record R;
  R.PrologEnd = 4;
  addRule(R.Prolog, 4, UOP_SaveFPR, 8, -16);
  std::vector<uint8_t> Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Bytes), Failed());
  R.RequiredState = STATE_Integer | STATE_Floating;
  Bytes = R.build();
  Expected<DecodedUnwindInfo> Info = decodeUnwindInfo(Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  EXPECT_EQ(evaluateState(*Info, 4).FPR[8].Kind, RuleKind::Saved);
}

TEST(RISCVWinEHTest, FrameRules) {
  Record R;
  R.PrologEnd = 8;
  R.RequiredState = STATE_Integer | STATE_Floating;
  addRule(R.Prolog, 4, UOP_SetCFA, 2, 800);
  addRule(R.Prolog, 8, UOP_Context, 0, -800);
  addRule(R.Prolog, 8, UOP_SaveGPR, 8, -808);
  std::vector<uint8_t> Bytes = R.build();
  Expected<DecodedUnwindInfo> Info = decodeUnwindInfo(Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  UnwindState S = evaluateState(*Info, 4);
  EXPECT_EQ(S.BaseFrame, BaseFrameKind::None);
  EXPECT_FALSE(S.hasExactCallerPC());
  S = evaluateState(*Info, 8);
  EXPECT_EQ(S.BaseFrame, BaseFrameKind::Context);
  EXPECT_EQ(S.BaseFrameOffset, -800);
  EXPECT_EQ(S.GPR[8].Kind, RuleKind::Saved);
  EXPECT_TRUE(S.hasExactCallerPC());

  Record M;
  M.PrologEnd = 4;
  addRule(M.Prolog, 4, UOP_MachineFrame, 0, 16);
  Bytes = M.build();
  Info = decodeUnwindInfo(Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  S = evaluateState(*Info, 4);
  ASSERT_TRUE(S.MachineFrameOffset);
  EXPECT_EQ(*S.MachineFrameOffset, 16);
  EXPECT_TRUE(S.hasExactCallerPC());
}

TEST(RISCVWinEHTest, ReturnSource) {
  Record R;
  addRule(R.Prolog, 0, UOP_ReturnFromGPR, 5, -4);
  std::vector<uint8_t> Bytes = R.build();
  Expected<DecodedUnwindInfo> Info = decodeUnwindInfo(Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  UnwindState S = evaluateState(*Info, 10);
  EXPECT_EQ(S.ReturnRegister, 5);
  EXPECT_EQ(S.ReturnAdjust, -4);
  EXPECT_FALSE(S.hasExactCallerPC());

  R.Prolog.clear();
  addRule(R.Prolog, 0, UOP_ReturnFromGPR, 5, -3);
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Bytes), Failed());
}

TEST(RISCVWinEHTest, EstablisherRegister) {
  Record R = makeBasicFrame();
  R.EstablisherRegister = 9;
  R.EstablisherFrameOffset = 0;
  std::vector<uint8_t> Bytes = R.build();
  Expected<DecodedUnwindInfo> Info = decodeUnwindInfo(Bytes, 30);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  EXPECT_EQ(Info->EstablisherRegister, 9);

  // A volatile register would not survive unwinding the frame's callees.
  R.EstablisherRegister = 5;
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Bytes, 30), Failed());
}

TEST(RISCVWinEHTest, RejectsMalformedRecords) {
  auto Rejects = [](const Record &R) {
    std::vector<uint8_t> Bytes = R.build();
    return !static_cast<bool>(expectedToOptional(decodeUnwindInfo(Bytes, 30)));
  };
  Record Good = makeBasicFrame();
  EXPECT_FALSE(Rejects(Good));

  Record BadHeader = Good;
  BadHeader.HeaderSize = UnwindHeaderSize + 4;
  EXPECT_TRUE(Rejects(BadHeader));

  Record Reserved = Good;
  Reserved.Reserved = 1;
  EXPECT_TRUE(Rejects(Reserved));

  Record SaveSP = Good;
  SaveSP.Prolog.clear();
  addRule(SaveSP.Prolog, 4, UOP_SaveGPR, 2, -8);
  EXPECT_TRUE(Rejects(SaveSP));

  Record ZeroScale = Good;
  ZeroScale.RequiredState = STATE_Integer | STATE_Vector;
  ZeroScale.Prolog.clear();
  addRule(ZeroScale.Prolog, 4, UOP_SetCFAScaled, 2, 16, 0);
  EXPECT_TRUE(Rejects(ZeroScale));

  Record Conflict = Good;
  Conflict.Prolog.clear();
  addRule(Conflict.Prolog, 4, UOP_SetCFA, 2, 16);
  addRule(Conflict.Prolog, 4, UOP_SetCFA, 2, 32);
  EXPECT_TRUE(Rejects(Conflict));

  Record Unsorted = Good;
  Unsorted.Prolog.clear();
  addRule(Unsorted.Prolog, 8, UOP_SetCFA, 2, 16);
  addRule(Unsorted.Prolog, 4, UOP_SaveGPR, 1, -8);
  EXPECT_TRUE(Rejects(Unsorted));

  // The large form of a rule must keep its inline operand zero.
  Record LargeInline = Good;
  LargeInline.Prolog.clear();
  LargeInline.Prolog.push_back(
      {4, UOP_SetCFALarge | (2u << RegisterShift) | (1u << OperandShift)});
  LargeInline.Prolog.push_back({64, 0});
  EXPECT_TRUE(Rejects(LargeInline));
}

} // namespace
