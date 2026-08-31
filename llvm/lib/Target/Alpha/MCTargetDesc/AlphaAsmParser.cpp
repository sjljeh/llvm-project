//===-- AlphaAsmParser.cpp - Minimal Alpha Assembly Parser ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaMCTargetDesc.h"
#include "TargetInfo/AlphaTargetInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

class AlphaOperand : public MCParsedAsmOperand {
public:
  enum KindTy { Token, Register, Immediate, Expression } Kind;

private:
  StringRef Tok;
  MCRegister Reg = 0;
  int64_t Imm = 0;
  const MCExpr *Expr = nullptr;
  SMLoc StartLoc;
  SMLoc EndLoc;

  AlphaOperand(KindTy Kind, SMLoc StartLoc, SMLoc EndLoc)
      : Kind(Kind), StartLoc(StartLoc), EndLoc(EndLoc) {}

public:
  static std::unique_ptr<AlphaOperand> createToken(StringRef Tok, SMLoc Loc) {
    auto Op = std::unique_ptr<AlphaOperand>(new AlphaOperand(Token, Loc, Loc));
    Op->Tok = Tok;
    return Op;
  }

  static std::unique_ptr<AlphaOperand> createReg(MCRegister Reg, SMLoc StartLoc,
                                                 SMLoc EndLoc) {
    auto Op = std::unique_ptr<AlphaOperand>(
        new AlphaOperand(Register, StartLoc, EndLoc));
    Op->Reg = Reg;
    return Op;
  }

  static std::unique_ptr<AlphaOperand> createImm(int64_t Imm, SMLoc StartLoc,
                                                 SMLoc EndLoc) {
    auto Op = std::unique_ptr<AlphaOperand>(
        new AlphaOperand(Immediate, StartLoc, EndLoc));
    Op->Imm = Imm;
    return Op;
  }

  static std::unique_ptr<AlphaOperand> createExpr(const MCExpr *Expr,
                                                  SMLoc StartLoc,
                                                  SMLoc EndLoc) {
    auto Op = std::unique_ptr<AlphaOperand>(
        new AlphaOperand(Expression, StartLoc, EndLoc));
    Op->Expr = Expr;
    return Op;
  }

  bool isToken() const override { return Kind == Token; }
  bool isImm() const override { return Kind == Immediate; }
  bool isReg() const override { return Kind == Register; }
  bool isExpr() const { return Kind == Expression; }
  bool isMem() const override { return false; }

  MCRegister getReg() const override { return Reg; }
  int64_t getImm() const { return Imm; }
  const MCExpr *getExpr() const { return Expr; }
  StringRef getToken() const { return Tok; }

  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  void print(raw_ostream &OS, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case Token:
      OS << Tok;
      break;
    case Register:
      OS << "register";
      break;
    case Immediate:
      OS << Imm;
      break;
    case Expression:
      OS << "expression";
      break;
    }
  }
};

class AlphaAsmParser : public MCTargetAsmParser {
public:
  AlphaAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                 const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII) {}

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                      SMLoc &EndLoc) override {
    return parseGPR(Reg, StartLoc, EndLoc);
  }

  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override {
    return ParseStatus::NoMatch;
  }

  bool ParseDirective(AsmToken DirectiveID) override {
    StringRef Directive = DirectiveID.getString();
    bool IsProcedureMetadata =
        StringSwitch<bool>(Directive.lower())
            .Cases({".aent", ".ent", ".end", ".edata"}, true)
            .Cases({".eflag", ".fmask", ".frame", ".livereg"}, true)
            .Cases({".prologue", ".save_ra"}, true);
    if (!IsProcedureMetadata)
      return true;

    // The 32-bit NT object streamer derives runtime-function records from
    // .seh directives. Accept legacy ASAXP procedure metadata for source
    // compatibility until its Alpha64 procedure-descriptor form is modeled.
    while (getLexer().isNot(AsmToken::EndOfStatement) &&
           getLexer().isNot(AsmToken::Eof))
      getParser().Lex();
    return parseEndOfStatement();
  }

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override {
    Operands.push_back(AlphaOperand::createToken(Name, NameLoc));

    unsigned Opcode = StringSwitch<unsigned>(Name)
                          .Case("unop", Alpha::UNOP)
                          .Case("mb", Alpha::MB)
                          .Case("wmb", Alpha::WMB)
                          .Case("call_pal", Alpha::CALL_PAL)
                          .Case("addl", Alpha::ADDLr)
                          .Case("addq", Alpha::ADDQr)
                          .Case("subl", Alpha::SUBLr)
                          .Case("subq", Alpha::SUBQr)
                          .Case("mull", Alpha::MULLr)
                          .Case("mulq", Alpha::MULQr)
                          .Case("and", Alpha::ANDr)
                          .Case("bic", Alpha::BICr)
                          .Case("bis", Alpha::BISr)
                          .Case("xor", Alpha::XORr)
                          .Case("srl", Alpha::SRLr)
                          .Case("zapnot", Alpha::ZAPNOTi)
                          .Case("cmpeq", Alpha::CMPEQ)
                          .Case("cmplt", Alpha::CMPLT)
                          .Case("cmpult", Alpha::CMPULT)
                          .Case("cmoveq", Alpha::CMOVEQr)
                          .Case("extwl", Alpha::EXTWL)
                          .Case("beq", Alpha::BEQ)
                          .Case("bne", Alpha::BNE)
                          .Case("br", Alpha::BR)
                          .Case("bsr", Alpha::BSR)
                          .Case("lda", Alpha::LDA)
                          .Case("ldah", Alpha::LDAH)
                          .Case("ldl", Alpha::LDL)
                          .Case("ldq", Alpha::LDQ)
                          .Case("ldq_u", Alpha::LDQ_U)
                          .Case("ldt", Alpha::LDT)
                          .Case("stl", Alpha::STL)
                          .Case("stq", Alpha::STQ)
                          .Case("stt", Alpha::STT)
                          .Case("ret", Alpha::RETDAG)
                          .Default(0);

    if (!Opcode)
      return Error(NameLoc, "Alpha instruction parsing is not implemented");

    if (Opcode == Alpha::UNOP || Opcode == Alpha::MB || Opcode == Alpha::WMB)
      return parseEndOfStatement();

    if (Opcode == Alpha::RETDAG) {
      // ASAXP accepts ret, ret $31,($26), and ret $31,($26),1. RETDAG has
      // those architectural defaults encoded, so consume optional operands.
      while (getLexer().isNot(AsmToken::EndOfStatement) &&
             getLexer().isNot(AsmToken::Eof))
        getParser().Lex();
      return parseEndOfStatement();
    }

    if (Opcode == Alpha::CALL_PAL)
      return parsePALInstruction(Operands);

    if (Opcode == Alpha::BR || Opcode == Alpha::BSR)
      return parseUnconditionalBranchInstruction(Operands);

    if (Opcode == Alpha::BEQ || Opcode == Alpha::BNE)
      return parseConditionalBranchInstruction(Operands);

    if (Opcode == Alpha::ADDLr || Opcode == Alpha::ADDQr ||
        Opcode == Alpha::SUBLr || Opcode == Alpha::SUBQr ||
        Opcode == Alpha::MULLr || Opcode == Alpha::MULQr ||
        Opcode == Alpha::ANDr || Opcode == Alpha::BICr ||
        Opcode == Alpha::BISr || Opcode == Alpha::XORr ||
        Opcode == Alpha::SRLr || Opcode == Alpha::ZAPNOTi ||
        Opcode == Alpha::CMPEQ || Opcode == Alpha::CMPLT ||
        Opcode == Alpha::CMPULT || Opcode == Alpha::CMOVEQr ||
        Opcode == Alpha::EXTWL)
      return parseOperateInstruction(Operands);

    return parseMemoryInstruction(Operands);
  }

  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                OperandVector &Operands, MCStreamer &Out,
                                uint64_t &ErrorInfo,
                                bool MatchingInlineAsm) override {
    auto &Mnemonic = static_cast<AlphaOperand &>(*Operands[0]);
    MCInst Inst;
    Inst.setLoc(IDLoc);

    StringRef Name = Mnemonic.getToken();
    unsigned InstOpcode = StringSwitch<unsigned>(Name)
                              .Case("unop", Alpha::UNOP)
                              .Case("mb", Alpha::MB)
                              .Case("wmb", Alpha::WMB)
                              .Case("call_pal", Alpha::CALL_PAL)
                              .Case("addl", Alpha::ADDLr)
                              .Case("addq", Alpha::ADDQr)
                              .Case("subl", Alpha::SUBLr)
                              .Case("subq", Alpha::SUBQr)
                              .Case("mull", Alpha::MULLr)
                              .Case("mulq", Alpha::MULQr)
                              .Case("and", Alpha::ANDr)
                              .Case("bic", Alpha::BICr)
                              .Case("bis", Alpha::BISr)
                              .Case("xor", Alpha::XORr)
                              .Case("srl", Alpha::SRLr)
                              .Case("zapnot", Alpha::ZAPNOTi)
                              .Case("cmpeq", Alpha::CMPEQ)
                              .Case("cmplt", Alpha::CMPLT)
                              .Case("cmpult", Alpha::CMPULT)
                              .Case("cmoveq", Alpha::CMOVEQr)
                              .Case("extwl", Alpha::EXTWL)
                              .Case("beq", Alpha::BEQ)
                              .Case("bne", Alpha::BNE)
                              .Case("br", Alpha::BR)
                              .Case("bsr", Alpha::BSR)
                              .Case("lda", Alpha::LDA)
                              .Case("ldah", Alpha::LDAH)
                              .Case("ldl", Alpha::LDL)
                              .Case("ldq", Alpha::LDQ)
                              .Case("ldq_u", Alpha::LDQ_U)
                              .Case("ldt", Alpha::LDT)
                              .Case("stl", Alpha::STL)
                              .Case("stq", Alpha::STQ)
                              .Case("stt", Alpha::STT)
                              .Case("ret", Alpha::RETDAG)
                              .Default(0);

    if (Name == "addl" || Name == "addq" || Name == "subl" ||
        Name == "subq" || Name == "mull" || Name == "mulq" ||
        Name == "and" || Name == "bic" || Name == "bis" ||
        Name == "xor" || Name == "srl" || Name == "zapnot" ||
        Name == "cmpeq" || Name == "cmplt" || Name == "cmpult" ||
        Name == "cmoveq" || Name == "extwl") {
      auto &Src2 = static_cast<AlphaOperand &>(*Operands[2]);
      if (Name == "addl")
        InstOpcode = Src2.isReg() ? Alpha::ADDLr : Alpha::ADDLi;
      else if (Name == "addq")
        InstOpcode = Src2.isReg() ? Alpha::ADDQr : Alpha::ADDQi;
      else if (Name == "subl")
        InstOpcode = Src2.isReg() ? Alpha::SUBLr : Alpha::SUBLi;
      else if (Name == "subq")
        InstOpcode = Src2.isReg() ? Alpha::SUBQr : Alpha::SUBQi;
      else if (Name == "mull")
        InstOpcode = Src2.isReg() ? Alpha::MULLr : Alpha::MULLi;
      else if (Name == "mulq")
        InstOpcode = Src2.isReg() ? Alpha::MULQr : Alpha::MULQi;
      else if (Name == "and")
        InstOpcode = Src2.isReg() ? Alpha::ANDr : Alpha::ANDi;
      else if (Name == "bic")
        InstOpcode = Src2.isReg() ? Alpha::BICr : Alpha::BICi;
      else if (Name == "bis")
        InstOpcode = Src2.isReg() ? Alpha::BISr : Alpha::BISi;
      else if (Name == "xor")
        InstOpcode = Src2.isReg() ? Alpha::XORr : Alpha::XORi;
      else if (Name == "srl")
        InstOpcode = Src2.isReg() ? Alpha::SRLr : Alpha::SRLi;
      else if (Name == "zapnot")
        InstOpcode = Alpha::ZAPNOTi;
      else if (Name == "cmpeq")
        InstOpcode = Src2.isReg() ? Alpha::CMPEQ : Alpha::CMPEQi;
      else if (Name == "cmplt")
        InstOpcode = Src2.isReg() ? Alpha::CMPLT : Alpha::CMPLTi;
      else if (Name == "cmpult")
        InstOpcode = Src2.isReg() ? Alpha::CMPULT : Alpha::CMPULTi;
      else if (Name == "cmoveq")
        InstOpcode = Src2.isReg() ? Alpha::CMOVEQr : Alpha::CMOVEQi;
      else if (Name == "extwl")
        InstOpcode = Src2.isReg() ? Alpha::EXTWL : Alpha::EXTWLi;
    }

    if (Name == "ldah" &&
        static_cast<AlphaOperand &>(*Operands[2]).isExpr())
      InstOpcode = Alpha::LDAHr;

    if (!InstOpcode)
      return Error(IDLoc, "Alpha instruction parsing is not implemented");

    Inst.setOpcode(InstOpcode);
    auto AddOperand = [&](unsigned I) {
      auto &Operand = static_cast<AlphaOperand &>(*Operands[I]);
      if (Operand.isReg())
        Inst.addOperand(MCOperand::createReg(Operand.getReg()));
      else if (Operand.isImm())
        Inst.addOperand(MCOperand::createImm(Operand.getImm()));
      else if (Operand.isExpr())
        Inst.addOperand(MCOperand::createExpr(Operand.getExpr()));
    };

    // Alpha operate instructions are written as RA,RB-or-literal,RC, while
    // their TableGen operand order is the def first: RC,RA,RB-or-literal.
    bool IsOperate =
        Name == "addl" || Name == "addq" || Name == "subl" ||
        Name == "subq" || Name == "mull" || Name == "mulq" ||
        Name == "and" || Name == "bic" || Name == "bis" ||
        Name == "xor" || Name == "srl" || Name == "zapnot" ||
        Name == "cmpeq" || Name == "cmplt" || Name == "cmpult" ||
        Name == "cmoveq" || Name == "extwl";
    if (IsOperate) {
      AddOperand(3);
      AddOperand(1);
      AddOperand(2);
    } else {
      for (unsigned I = 1, E = Operands.size(); I != E; ++I)
        AddOperand(I);
    }

    Out.emitInstruction(Inst, getSTI());
    return false;
  }

  void convertToMapAndConstraints(unsigned Kind,
                                   const OperandVector &Operands) override {}

private:
  bool parseGPR(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) {
    StartLoc = getParser().getTok().getLoc();

    if (getLexer().is(AsmToken::Dollar))
      getParser().Lex();

    bool IsFPR = false;
    int64_t RegNo;
    if (getLexer().is(AsmToken::Integer)) {
      RegNo = getParser().getTok().getIntVal();
    } else if (getLexer().is(AsmToken::Identifier)) {
      StringRef Name = getParser().getTok().getString();
      if (!Name.consume_front("f") || Name.getAsInteger(10, RegNo))
        return true;
      IsFPR = true;
    } else {
      return true;
    }

    if (RegNo < 0 || RegNo > 31)
      return Error(StartLoc, "invalid Alpha register");

    static const MCRegister GPRs[] = {
        Alpha::R0,  Alpha::R1,  Alpha::R2,  Alpha::R3,
        Alpha::R4,  Alpha::R5,  Alpha::R6,  Alpha::R7,
        Alpha::R8,  Alpha::R9,  Alpha::R10, Alpha::R11,
        Alpha::R12, Alpha::R13, Alpha::R14, Alpha::R15,
        Alpha::R16, Alpha::R17, Alpha::R18, Alpha::R19,
        Alpha::R20, Alpha::R21, Alpha::R22, Alpha::R23,
        Alpha::R24, Alpha::R25, Alpha::R26, Alpha::R27,
        Alpha::R28, Alpha::R29, Alpha::R30, Alpha::R31};
    static const MCRegister FPRs[] = {
        Alpha::F0,  Alpha::F1,  Alpha::F2,  Alpha::F3,
        Alpha::F4,  Alpha::F5,  Alpha::F6,  Alpha::F7,
        Alpha::F8,  Alpha::F9,  Alpha::F10, Alpha::F11,
        Alpha::F12, Alpha::F13, Alpha::F14, Alpha::F15,
        Alpha::F16, Alpha::F17, Alpha::F18, Alpha::F19,
        Alpha::F20, Alpha::F21, Alpha::F22, Alpha::F23,
        Alpha::F24, Alpha::F25, Alpha::F26, Alpha::F27,
        Alpha::F28, Alpha::F29, Alpha::F30, Alpha::F31};

    Reg = IsFPR ? FPRs[RegNo] : GPRs[RegNo];
    EndLoc = getParser().getTok().getEndLoc();
    getParser().Lex();
    return false;
  }

  bool parseRegisterOperand(OperandVector &Operands) {
    MCRegister Reg;
    SMLoc StartLoc;
    SMLoc EndLoc;
    if (parseGPR(Reg, StartLoc, EndLoc))
      return Error(getParser().getTok().getLoc(), "expected Alpha register");
    Operands.push_back(AlphaOperand::createReg(Reg, StartLoc, EndLoc));
    return false;
  }

  bool parseImmediateOperand(OperandVector &Operands) {
    SMLoc StartLoc = getParser().getTok().getLoc();
    int64_t Imm;
    if (getParser().parseAbsoluteExpression(Imm))
      return true;
    SMLoc EndLoc = SMLoc::getFromPointer(StartLoc.getPointer() - 1);
    Operands.push_back(AlphaOperand::createImm(Imm, StartLoc, EndLoc));
    return false;
  }

  bool parseImmediateOrExpression(OperandVector &Operands,
                                  bool AllowDollarPrefix = false) {
    SMLoc StartLoc = getParser().getTok().getLoc();

    if (AllowDollarPrefix && getLexer().is(AsmToken::Dollar))
      getParser().Lex();

    // ASAXP supports multi-digit directional labels (1 through 255).
    if (getLexer().is(AsmToken::Integer) &&
        getLexer().peekTok().is(AsmToken::Identifier)) {
      AsmToken Label = getParser().getTok();
      StringRef Direction = getLexer().peekTok().getString();
      if (Direction == "f" || Direction == "b") {
        int64_t Number = Label.getIntVal();
        if (Number < 1 || Number > 255)
          return Error(StartLoc, "directional label must be between 1 and 255");
        getParser().Lex();
        MCSymbol *Symbol = getContext().getDirectionalLocalSymbol(
            Number, Direction == "b");
        const MCExpr *Expr = MCSymbolRefExpr::create(Symbol, getContext());
        SMLoc EndLoc = getParser().getTok().getEndLoc();
        getParser().Lex();
        Operands.push_back(AlphaOperand::createExpr(Expr, StartLoc, EndLoc));
        return false;
      }
    }

    // Numeric branch operands in the established GNU-style syntax are encoded
    // instruction displacements, not byte-address expressions.
    if (getLexer().is(AsmToken::Integer) || getLexer().is(AsmToken::Plus) ||
        getLexer().is(AsmToken::Minus))
      return parseImmediateOperand(Operands);

    // Alpha's canonical call spelling uses symbol..ng. BRADDR already carries
    // the non-GP call semantics in COFF, so the suffix is not part of the name.
    if (getLexer().is(AsmToken::Identifier) &&
        getParser().getTok().getString().ends_with("..ng")) {
      StringRef Name = getParser().getTok().getString().drop_back(4);
      const MCExpr *Expr =
          MCSymbolRefExpr::create(getContext().getOrCreateSymbol(Name),
                                  getContext());
      SMLoc EndLoc = getParser().getTok().getEndLoc();
      getParser().Lex();
      Operands.push_back(AlphaOperand::createExpr(Expr, StartLoc, EndLoc));
      return false;
    }

    const MCExpr *Expr;
    if (getParser().parseExpression(Expr))
      return true;
    SMLoc EndLoc = getParser().getTok().getLoc();
    if (getLexer().is(AsmToken::Identifier) &&
        getParser().getTok().getString() == "..ng")
      getParser().Lex();
    Operands.push_back(AlphaOperand::createExpr(Expr, StartLoc, EndLoc));
    return false;
  }

  bool parseComma() {
    if (!parseOptionalToken(AsmToken::Comma))
      return Error(getParser().getTok().getLoc(), "expected ','");
    return false;
  }

  bool parseEndOfStatement() {
    if (getLexer().isNot(AsmToken::EndOfStatement))
      return Error(getParser().getTok().getLoc(), "unexpected token");
    getParser().Lex();
    return false;
  }

  bool parseOperateInstruction(OperandVector &Operands) {
    if (parseRegisterOperand(Operands) || parseComma())
      return true;

    if (getLexer().is(AsmToken::Dollar)) {
      if (parseRegisterOperand(Operands))
        return true;
    } else {
      if (parseImmediateOperand(Operands))
        return true;
    }

    if (parseComma() || parseRegisterOperand(Operands))
      return true;
    return parseEndOfStatement();
  }

  bool parseMemoryInstruction(OperandVector &Operands) {
    if (parseRegisterOperand(Operands) || parseComma() ||
        parseImmediateOrExpression(Operands))
      return true;

    if (!parseOptionalToken(AsmToken::LParen))
      return Error(getParser().getTok().getLoc(), "expected '('");
    if (parseRegisterOperand(Operands))
      return true;
    if (!parseOptionalToken(AsmToken::RParen))
      return Error(getParser().getTok().getLoc(), "expected ')'");
    if (getLexer().is(AsmToken::Exclaim)) {
      getParser().Lex();
      if (getLexer().is(AsmToken::Identifier))
        getParser().Lex();
    }
    return parseEndOfStatement();
  }

  bool parsePALInstruction(OperandVector &Operands) {
    if (parseImmediateOperand(Operands))
      return true;
    return parseEndOfStatement();
  }

  bool parseConditionalBranchInstruction(OperandVector &Operands) {
    if (parseRegisterOperand(Operands) || parseComma() ||
        parseImmediateOrExpression(Operands, true))
      return true;
    return parseEndOfStatement();
  }

  bool parseUnconditionalBranchInstruction(OperandVector &Operands) {
    // The canonical printer includes the fixed link register ($31 for br,
    // $26 for bsr), while the compact form historically accepted by this
    // parser omits it. Accept both spellings.
    if (getLexer().is(AsmToken::Dollar) &&
        getLexer().peekTok().is(AsmToken::Integer)) {
      MCRegister IgnoredReg;
      SMLoc StartLoc, EndLoc;
      if (parseGPR(IgnoredReg, StartLoc, EndLoc) || parseComma())
        return true;
    }
    if (parseImmediateOrExpression(Operands, true))
      return true;
    return parseEndOfStatement();
  }
};

} // end anonymous namespace

void llvm::registerAlphaAsmParser() {
  RegisterMCAsmParser<AlphaAsmParser> X(getTheAlphaTarget());
}
