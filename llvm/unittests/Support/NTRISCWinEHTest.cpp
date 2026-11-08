//===- NTRISCWinEHTest.cpp - Alpha/MIPS/PowerPC NTUW decoder tests --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/NTRISCWinEH.h"
#include "llvm/Testing/Support/Error.h"
#include "gtest/gtest.h"
#include <vector>

using namespace llvm;
using namespace llvm::NTRISCWinEH;

namespace {

using Slots = std::vector<std::pair<uint32_t, uint32_t>>;

void addRule(Slots &Program, uint32_t CodeOffset, unsigned Opcode,
             unsigned Register, int32_t Operand) {
  for (auto Slot : encodeRule(CodeOffset, Opcode, Register, Operand))
    Program.push_back(Slot);
}

struct Record {
  uint32_t Flags = 0;
  uint32_t PrologEnd = 0;
  int32_t EstablisherFrameOffset = 0;
  uint16_t RequiredState = STATE_Integer;
  uint8_t EstablisherRegister = 0;
  uint32_t FunctionLength = 0;
  Slots Prolog;
  struct Scope {
    uint32_t Start, End;
    Slots Codes;
  };
  std::vector<Scope> Epilogs;
  std::vector<uint32_t> Tail;

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
                    EpilogSlots * UnwindCodeSize + Tail.size() * 4;
    Put32(UnwindMagic);
    Put16(UnwindHeaderSize);
    Bytes.push_back(EstablisherRegister);
    Bytes.push_back(0);
    Put32(Size);
    Put32(Flags);
    Put32(PrologEnd);
    Put32(uint32_t(EstablisherFrameOffset));
    Put16(Prolog.size());
    Put16(Epilogs.size());
    Put16(EpilogSlots);
    Put16(RequiredState);
    Put32(FunctionLength);
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
    for (uint32_t Word : Tail)
      Put32(Word);
    return Bytes;
  }
};

/// MIPS: addiu sp,sp,-32 / sw ra,28(sp) / sw s0,24(sp) / body /
/// lw s0,24(sp) / lw ra,28(sp) / jr ra / addiu sp,sp,32.
Record makeMIPSFrame() {
  Record R;
  R.FunctionLength = 40;
  R.PrologEnd = 12;
  addRule(R.Prolog, 4, UOP_SetCFA, 29, 32);
  addRule(R.Prolog, 8, UOP_SaveGPR, 31, -4);
  addRule(R.Prolog, 12, UOP_SaveGPR, 16, -8);
  Record::Scope Epilog{24, 40, {}};
  addRule(Epilog.Codes, 4, UOP_SameGPR, 16, 0);
  addRule(Epilog.Codes, 8, UOP_SameGPR, 31, 0);
  // The release is in the jr delay slot, so it holds after the slot.
  addRule(Epilog.Codes, 16, UOP_SetCFA, 29, 0);
  R.Epilogs.push_back(Epilog);
  return R;
}

TEST(NTRISCWinEHTest, EvaluatesMIPSFrame) {
  std::vector<uint8_t> Bytes = makeMIPSFrame().build();
  auto Info = decodeUnwindInfo(Machine::MIPS, Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  UnwindState Entry = getEntryState(Machine::MIPS);
  EXPECT_EQ(Entry.CFARegister, 29);
  EXPECT_EQ(Entry.ReturnRegister, 31);

  UnwindState S = evaluateState(*Info, 0, Entry);
  EXPECT_EQ(S, Entry);
  S = evaluateState(*Info, 4, Entry);
  EXPECT_EQ(S.CFAOffset, 32);
  EXPECT_EQ(S.GPR[31].Kind, RuleKind::Unchanged);
  S = evaluateState(*Info, 8, Entry);
  EXPECT_EQ(S.GPR[31].Kind, RuleKind::Saved);
  EXPECT_EQ(S.GPR[31].Offset, -4);
  EXPECT_EQ(S.GPR[16].Kind, RuleKind::Unchanged);

  UnwindState Body = evaluateState(*Info, 16, Entry);
  EXPECT_EQ(Body.GPR[16].Offset, -8);
  // Code after the epilogue scope's start but before its first rule.
  EXPECT_EQ(evaluateState(*Info, 24, Entry), Body);
  S = evaluateState(*Info, 32, Entry);
  EXPECT_EQ(S.GPR[16].Kind, RuleKind::Unchanged);
  EXPECT_EQ(S.GPR[31].Kind, RuleKind::Unchanged);
  // The jr and its delay slot both still see the frame.
  S = evaluateState(*Info, 36, Entry);
  EXPECT_EQ(S.CFAOffset, 32);
  // The return address of a call that ends the function sees the body.
  EXPECT_EQ(evaluateState(*Info, 40, Entry), Body);

  DecodedUnwindInfo I = *Info;
  std::array<uint64_t, 32> Regs{};
  Regs[29] = 0x1000;
  EXPECT_EQ(getEstablisherFrame(I, Body, Regs), 0x1020u);
}

TEST(NTRISCWinEHTest, PowerPCSpecialRegisters) {
  // mflr r0 / stw r0,-8(r1) / stwu r1,-64(r1) / body / ... / blr
  Record R;
  R.FunctionLength = 32;
  R.PrologEnd = 12;
  addRule(R.Prolog, 8, UOP_SaveSPR, SPR_LR, -8);
  addRule(R.Prolog, 12, UOP_SetCFA, 1, 64);
  std::vector<uint8_t> Bytes = R.build();
  auto Info = decodeUnwindInfo(Machine::PPC, Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  // SAVE_SPR always takes the two-slot form.
  EXPECT_EQ(Info->PrologCodeSlots, 3);
  EXPECT_EQ(Info->PrologCodes[0].SlotCount, 2);
  UnwindState Entry = getEntryState(Machine::PPC);
  EXPECT_TRUE(Entry.ReturnFromLR);
  UnwindState S = evaluateState(*Info, 16, Entry);
  EXPECT_EQ(S.SPR[SPR_LR].Kind, RuleKind::Saved);
  EXPECT_EQ(S.SPR[SPR_LR].Offset, -8);
  EXPECT_EQ(S.CFAOffset, 64);

  // Special registers do not exist on the other machines.
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::MIPS, Bytes), Failed());
}

TEST(NTRISCWinEHTest, LargeOperands) {
  Record R;
  R.FunctionLength = 16;
  R.PrologEnd = 12;
  addRule(R.Prolog, 12, UOP_SetCFA, 30, 0x200000);
  std::vector<uint8_t> Bytes = R.build();
  auto Info = decodeUnwindInfo(Machine::Alpha, Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  EXPECT_EQ(Info->PrologCodes[0].Opcode, UOP_SetCFALarge);
  EXPECT_EQ(Info->PrologCodes[0].Operand, 0x200000);
  EXPECT_EQ(evaluateState(*Info, 12, getEntryState(Machine::Alpha)).CFAOffset,
            0x200000);
}

TEST(NTRISCWinEHTest, RejectsMachineViolations) {
  auto Rejects = [](Machine M, unsigned Opcode, unsigned Reg, int32_t Op) {
    Record R;
    R.FunctionLength = 8;
    R.PrologEnd = 4;
    R.RequiredState = STATE_Integer | STATE_Floating;
    addRule(R.Prolog, 4, Opcode, Reg, Op);
    std::vector<uint8_t> Bytes = R.build();
    Expected<DecodedUnwindInfo> Result = decodeUnwindInfo(M, Bytes);
    if (Result)
      return false;
    consumeError(Result.takeError());
    return true;
  };
  // Saves of the zero register or sp.
  EXPECT_TRUE(Rejects(Machine::Alpha, UOP_SaveGPR, 31, 0));
  EXPECT_TRUE(Rejects(Machine::Alpha, UOP_SaveGPR, 30, 0));
  EXPECT_TRUE(Rejects(Machine::MIPS, UOP_SaveGPR, 0, 0));
  EXPECT_TRUE(Rejects(Machine::MIPS, UOP_SaveGPR, 29, 0));
  EXPECT_TRUE(Rejects(Machine::PPC, UOP_SaveGPR, 1, 0));
  // Register images are aligned to their size.
  EXPECT_TRUE(Rejects(Machine::Alpha, UOP_SaveGPR, 9, -4));
  EXPECT_FALSE(Rejects(Machine::MIPS, UOP_SaveGPR, 16, -4));
  // MIPS FPRs are named by the even register of a pair.
  EXPECT_TRUE(Rejects(Machine::MIPS, UOP_SaveFPR, 21, -8));
  EXPECT_FALSE(Rejects(Machine::MIPS, UOP_SaveFPR, 20, -8));
  EXPECT_TRUE(Rejects(Machine::Alpha, UOP_SaveFPR, 31, -8));
  // The CFA cannot be based on a zero register.
  EXPECT_TRUE(Rejects(Machine::Alpha, UOP_SetCFA, 31, 16));
  EXPECT_FALSE(Rejects(Machine::PPC, UOP_SetCFA, 0, 16));
  // RVUW's vector opcodes do not exist in NTUW.
  EXPECT_TRUE(Rejects(Machine::Alpha, 0x0a, 0, 0));
}

TEST(NTRISCWinEHTest, RejectsMalformedRecords) {
  Record R = makeMIPSFrame();
  R.FunctionLength = 38;
  std::vector<uint8_t> Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::MIPS, Bytes), Failed());

  R = makeMIPSFrame();
  R.Epilogs[0].Start = 8; // Inside the prologue.
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::MIPS, Bytes), Failed());

  R = makeMIPSFrame();
  R.Prolog[0].first = 6; // Not an instruction boundary.
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::MIPS, Bytes), Failed());

  R = makeMIPSFrame();
  R.Flags = UNW_ExceptionHandler; // No handler tail.
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::MIPS, Bytes), Failed());

  R = makeMIPSFrame();
  R.EstablisherRegister = 8; // A volatile register.
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::MIPS, Bytes), Failed());
  R.EstablisherRegister = 30;
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::MIPS, Bytes), Succeeded());
}

TEST(NTRISCWinEHTest, HandlerAndChain) {
  Record R = makeMIPSFrame();
  R.Flags = UNW_ExceptionHandler | UNW_TerminateHandler;
  R.Tail = {0x1000, 0x2000};
  std::vector<uint8_t> Bytes = R.build();
  auto Info = decodeUnwindInfo(Machine::MIPS, Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  ASSERT_TRUE(Info->HandlerInfo);
  EXPECT_EQ(Info->HandlerInfo->first, 0x1000u);
  EXPECT_EQ(Info->HandlerInfo->second, 0x2000u);

  Record C;
  C.FunctionLength = 64;
  C.Flags = UNW_ChainInfo;
  C.Tail = {0x400, 0x3000};
  Bytes = C.build();
  Info = decodeUnwindInfo(Machine::PPC, Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  ASSERT_TRUE(Info->Chained);
  EXPECT_EQ(Info->Chained->first, 0x400u);
}

TEST(NTRISCWinEHTest, LegacyRecord) {
  Record R;
  R.FunctionLength = 0x40;
  R.PrologEnd = 0x10;
  R.Flags = UNW_Legacy;
  R.Tail = {0x10001000, 0x10001040, 0x10002000, 0x10003000, 0x10001010};
  std::vector<uint8_t> Bytes = R.build();
  auto Info = decodeUnwindInfo(Machine::Alpha, Bytes);
  ASSERT_THAT_EXPECTED(Info, Succeeded());
  ASSERT_TRUE(Info->Legacy);
  EXPECT_EQ(Info->Legacy->ExceptionHandler, 0x10002000u);

  // The embedded entry must agree with the header.
  R.Tail[4] = 0x10001014;
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::Alpha, Bytes), Failed());
  // A legacy record carries no state program.
  R.Tail[4] = 0x10001010;
  addRule(R.Prolog, 4, UOP_SetCFA, 30, 16);
  Bytes = R.build();
  EXPECT_THAT_EXPECTED(decodeUnwindInfo(Machine::Alpha, Bytes), Failed());
}

TEST(NTRISCWinEHTest, ClassifiesExceptionDirectories) {
  // An image of 0x100 bytes with an NTUW record at RVA 0x40.
  std::vector<uint8_t> Image(0x100, 0);
  std::vector<uint8_t> Record = makeMIPSFrame().build();
  std::copy(Record.begin(), Record.end(), Image.begin() + 0x40);
  auto Read = [&](uint32_t RVA, uint32_t Size) -> ArrayRef<uint8_t> {
    if (RVA >= Image.size())
      return {};
    return ArrayRef<uint8_t>(Image).slice(
        RVA, std::min<size_t>(Size, Image.size() - RVA));
  };
  auto Words = [](std::initializer_list<uint32_t> List) {
    std::vector<uint8_t> Bytes;
    for (uint32_t W : List)
      for (int I = 0; I != 4; ++I)
        Bytes.push_back(uint8_t(W >> (8 * I)));
    return Bytes;
  };

  EXPECT_EQ(classifyExceptionDirectory({}, Read), TableFormat::Empty);
  EXPECT_EQ(classifyExceptionDirectory(Words({0x10, 0x40}), Read),
            TableFormat::NTUW);
  // NT 4.0 entries hold absolute addresses; the second word (EndAddress)
  // does not lead to an NTUW record.
  std::vector<uint8_t> Legacy = Words({0x10010, 0x10040, 0, 0, 0x10018});
  EXPECT_EQ(classifyExceptionDirectory(Legacy, Read), TableFormat::Legacy);
  // A size that is a multiple of both, without the magic, is NT 4.0.
  std::vector<uint8_t> Two = Legacy;
  Two.insert(Two.end(), Legacy.begin(), Legacy.end());
  EXPECT_EQ(classifyExceptionDirectory(Two, Read), TableFormat::Legacy);
  // Misaligned or out-of-image UnwindData is not NTUW.
  EXPECT_EQ(classifyExceptionDirectory(Words({0x10, 0x42}), Read),
            TableFormat::Invalid);
  EXPECT_EQ(classifyExceptionDirectory(Words({0x10, 0x1000}), Read),
            TableFormat::Invalid);
  // Neither size.
  EXPECT_EQ(classifyExceptionDirectory(Words({0x10, 0x40, 0}), Read),
            TableFormat::Invalid);
}

} // namespace
