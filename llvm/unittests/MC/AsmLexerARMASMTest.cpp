//===- llvm/unittests/MC/AsmLexerARMASMTest.cpp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCTargetOptions.h"
#include "gtest/gtest.h"

using namespace llvm;

namespace {

class AsmLexerARMASMTest : public ::testing::Test {
protected:
  MCTargetOptions Options;
  MCAsmInfo MAI{Options};
  AsmLexer Lexer{MAI};
  std::string Buffer;

  void lex(StringRef Source, bool ARMASM = true) {
    Buffer = Source.str();
    Lexer.setLexARMASM(ARMASM);
    Lexer.setBuffer(Buffer);
    Lexer.Lex();
  }

  void expectInteger(int64_t Value, StringRef Spelling) {
    ASSERT_EQ(Lexer.getKind(), AsmToken::Integer);
    EXPECT_EQ(Lexer.getTok().getIntVal(), Value);
    EXPECT_EQ(Lexer.getTok().getString(), Spelling);
    Lexer.Lex();
  }
};

TEST_F(AsmLexerARMASMTest, SemicolonStartsComment) {
  lex("mov ; comment\nret");
  ASSERT_EQ(Lexer.getKind(), AsmToken::Identifier);
  EXPECT_EQ(Lexer.getTok().getString(), "mov");
  Lexer.Lex();
  EXPECT_EQ(Lexer.getKind(), AsmToken::EndOfStatement);
  Lexer.Lex();
  ASSERT_EQ(Lexer.getKind(), AsmToken::Identifier);
  EXPECT_EQ(Lexer.getTok().getString(), "ret");
}

TEST_F(AsmLexerARMASMTest, QuotedIdentifierKeepsBars) {
  lex("|.text$mn|, |a b|");
  ASSERT_EQ(Lexer.getKind(), AsmToken::Identifier);
  EXPECT_EQ(Lexer.getTok().getString(), "|.text$mn|");
  Lexer.Lex();
  EXPECT_EQ(Lexer.getKind(), AsmToken::Comma);
  Lexer.Lex();
  ASSERT_EQ(Lexer.getKind(), AsmToken::Identifier);
  EXPECT_EQ(Lexer.getTok().getString(), "|a b|");
}

TEST_F(AsmLexerARMASMTest, UnterminatedBarIsPipe) {
  lex("1 | 2");
  expectInteger(1, "1");
  EXPECT_EQ(Lexer.getKind(), AsmToken::Pipe);
}

TEST_F(AsmLexerARMASMTest, AmpersandHexInteger) {
  lex("&FF &1f");
  expectInteger(255, "&FF");
  expectInteger(31, "&1f");
}

TEST_F(AsmLexerARMASMTest, BaseNInteger) {
  lex("2_101 8_17 9_8");
  expectInteger(5, "2_101");
  expectInteger(15, "8_17");
  expectInteger(8, "9_8");
}

TEST_F(AsmLexerARMASMTest, InvalidBaseNDigitIsError) {
  lex("2_102");
  EXPECT_EQ(Lexer.getKind(), AsmToken::Error);
}

TEST_F(AsmLexerARMASMTest, DisabledModeKeepsGNUBehaviour) {
  lex("&FF", /*ARMASM=*/false);
  EXPECT_EQ(Lexer.getKind(), AsmToken::Amp);
  lex("a ; b", /*ARMASM=*/false);
  ASSERT_EQ(Lexer.getKind(), AsmToken::Identifier);
  Lexer.Lex();
  EXPECT_EQ(Lexer.getKind(), AsmToken::EndOfStatement);
  EXPECT_EQ(Lexer.getTok().getString(), ";");
}

} // namespace
