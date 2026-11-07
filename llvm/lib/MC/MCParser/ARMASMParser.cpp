//===- ARMASMParser.cpp - Parser for ARMASM-dialect assembly -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A target-neutral parser for the ARMASM dialect accepted by Microsoft armasm
// and armasm64. Like those assemblers it makes two passes over the source:
//
//   Pass 1 runs assembly control (IF/WHILE/MACRO/GET), assembly-time
//   variables and constants, and records the definitions that ARMASM lets
//   source use before they appear (register aliases, storage-map fields,
//   symbol sizes and exports). It does not touch the streamer.
//
//   Pass 2 repeats assembly control and emits every statement directly to
//   the MCStreamer. Instruction lines are handed to the target's
//   MCTargetAsmParser, which calls back into parseExpression() for operand
//   expressions so that ARMASM operators, variables and built-ins work inside
//   instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCParser/ARMASMParser.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCWinCOFFObjectWriter.h"
#include "llvm/MC/MCWinCOFFStreamer.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SHA256.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;

namespace {

//===----------------------------------------------------------------------===//
// Assembly-time values
//===----------------------------------------------------------------------===//

enum class VariableKind {
  Arithmetic,
  Logical,
  String,
  RegisterRelative,
  /// A relocatable (or not yet known) value, only produced when the caller
  /// allows symbolic results.
  Symbolic,
};

struct VariableValue {
  VariableKind Kind = VariableKind::Arithmetic;
  uint64_t Arithmetic = 0;
  bool Logical = false;
  std::string String;
  uint64_t RegisterBase = 0;
  const MCExpr *Expr = nullptr;

  static VariableValue arithmetic(uint64_t Value) {
    VariableValue Result;
    Result.Arithmetic = Value;
    return Result;
  }

  static VariableValue logical(bool Value) {
    VariableValue Result;
    Result.Kind = VariableKind::Logical;
    Result.Logical = Value;
    return Result;
  }

  static VariableValue string(std::string Value) {
    VariableValue Result;
    Result.Kind = VariableKind::String;
    Result.String = std::move(Value);
    return Result;
  }

  static VariableValue registerRelative(uint64_t Base, uint64_t Offset) {
    VariableValue Result;
    Result.Kind = VariableKind::RegisterRelative;
    Result.RegisterBase = Base;
    Result.Arithmetic = Offset;
    return Result;
  }

  static VariableValue symbolic(const MCExpr *Expr) {
    VariableValue Result;
    Result.Kind = VariableKind::Symbolic;
    Result.Expr = Expr;
    return Result;
  }
};

using VariableMap = StringMap<VariableValue>;

/// The variables visible at a point in the source: the innermost macro's
/// local variables, if any, shadowing the global ones.
class VariableLookup {
  const VariableMap &Globals;
  const VariableMap *Locals;

public:
  VariableLookup(const VariableMap &Globals, const VariableMap *Locals = nullptr)
      : Globals(Globals), Locals(Locals) {}

  const VariableValue *find(StringRef Name) const {
    if (Locals)
      if (auto It = Locals->find(Name); It != Locals->end())
        return &It->second;
    if (auto It = Globals.find(Name); It != Globals.end())
      return &It->second;
    return nullptr;
  }

  bool contains(StringRef Name) const { return find(Name); }
};

std::optional<VariableValue>
getBuiltinVariable(StringRef Name, const VariableMap *Context = nullptr) {
  if (Context)
    for (const auto &Variable : *Context)
      if (Variable.getKey().equals_insensitive(Name))
        return Variable.getValue();
  if (Name.equals_insensitive("TRUE"))
    return VariableValue::logical(true);
  if (Name.equals_insensitive("FALSE"))
    return VariableValue::logical(false);
  if (Name.equals_insensitive("ARCHITECTURE"))
    return VariableValue::string("not specified");
  if (Name.equals_insensitive("CPU"))
    return VariableValue::string("\"-arch 4t\"");
  if (Name.equals_insensitive("CODESIZE") ||
      Name.equals_insensitive("CONFIG"))
    return VariableValue::arithmetic(32);
  if (Name.equals_insensitive("ENDIAN"))
    return VariableValue::string("little");
  if (Name.equals_insensitive("FPU"))
    return VariableValue::string("vfpv3");
  if (Name.equals_insensitive("OPT"))
    return VariableValue::arithmetic(1);
  return std::nullopt;
}

bool isBuiltinVariable(StringRef Name, const VariableMap *Context = nullptr) {
  return getBuiltinVariable(Name, Context).has_value();
}

StringRef variableKindName(VariableKind Kind) {
  switch (Kind) {
  case VariableKind::Arithmetic:
    return "arithmetic";
  case VariableKind::Logical:
    return "logical";
  case VariableKind::String:
    return "string";
  case VariableKind::RegisterRelative:
    return "register-relative";
  case VariableKind::Symbolic:
    return "relocatable";
  }
  llvm_unreachable("unknown ARMASM variable type");
}

//===----------------------------------------------------------------------===//
// Expression evaluation
//===----------------------------------------------------------------------===//

/// Resolves names that the evaluator cannot resolve from variables alone.
/// The parser implements this; it knows constants, symbols and the location
/// counter.
class ExpressionContext {
public:
  virtual ~ExpressionContext() = default;

  /// An absolute EQU constant.
  virtual std::optional<uint64_t> lookUpConstant(StringRef Name) const = 0;
  /// A value with a base register (storage-map field with a base).
  virtual std::optional<VariableValue>
  lookUpRegisterRelative(StringRef Name) const = 0;
  /// The size of a symbol, for ?symbol.
  virtual std::optional<uint64_t> lookUpSymbolSize(StringRef Name) const = 0;
  /// Whether :DEF: should consider \p Name defined.
  virtual bool isSymbolDefined(StringRef Name) const = 0;
  /// The built-in variables ({AREANAME}, {VAR}, ...).
  virtual const VariableMap *getBuiltins() const = 0;

  /// Symbolic resolution, only used when the caller asked for symbolic
  /// results. Returns null if \p Name cannot be referenced symbolically.
  virtual const MCExpr *getSymbolExpr(StringRef Name) { return nullptr; }
  /// The location counter: {PC} (\p IsPC) or '.'.
  virtual const MCExpr *getLocationExpr(bool IsPC) { return nullptr; }
  /// A forward ?symbol reference.
  virtual const MCExpr *getSymbolSizeExpr(StringRef Name) { return nullptr; }
  /// A numeric local label reference %[F|B][A|T]n[routname].
  virtual const MCExpr *getNumericLabelExpr(char Direction, unsigned Number,
                                            StringRef RoutName,
                                            std::string &Error) {
    return nullptr;
  }
  virtual MCContext *getContext() { return nullptr; }
  /// The register number :RCONST: yields for \p Name. Returns false to use
  /// the AArch64 (armasm64) rules; otherwise sets \p Number, or leaves it
  /// unset when \p Name is not a register of the dialect.
  virtual bool lookUpRegisterConstant(StringRef Name,
                                      std::optional<unsigned> &Number) const {
    return false;
  }
};

class ExpressionParser {
  enum class BinaryOperator {
    Multiply,
    Divide,
    Modulo,
    Concatenate,
    Left,
    Right,
    RotateLeft,
    RotateRight,
    ShiftLeft,
    ShiftRight,
    Add,
    Subtract,
    And,
    Or,
    ExclusiveOr,
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    LogicalAnd,
    LogicalOr,
    LogicalExclusiveOr,
  };

  struct ParsedOperator {
    BinaryOperator Operator;
    unsigned Precedence;
  };

  StringRef Text;
  VariableLookup Variables;
  ExpressionContext &Context;
  bool NoEscape;
  bool AllowSymbolic;
  /// Stop at the first character that cannot continue the expression instead
  /// of requiring the whole text to be consumed.
  bool Partial;
  size_t Position = 0;
  std::string ErrorMessage;

  void skipSpace() {
    while (Position != Text.size() && (Text[Position] == ' ' ||
                                       Text[Position] == '\t'))
      ++Position;
  }

  bool consume(StringRef Token) {
    if (!Text.drop_front(Position).starts_with(Token))
      return false;
    Position += Token.size();
    return true;
  }

  bool consumeInsensitive(StringRef Token) {
    if (!Text.drop_front(Position).starts_with_insensitive(Token))
      return false;
    Position += Token.size();
    return true;
  }

  bool fail(const Twine &Message) {
    if (ErrorMessage.empty())
      ErrorMessage = Message.str();
    return false;
  }

  static bool isIdentifierStart(char C) { return isAlpha(C) || C == '_'; }

  static bool isIdentifierChar(char C) {
    return isAlnum(C) || C == '_' || C == '.' || C == '$' || C == '?';
  }

  bool parseIdentifier(StringRef &Name, bool *WasQuoted = nullptr) {
    skipSpace();
    size_t Start = Position;
    if (WasQuoted)
      *WasQuoted = false;
    if (consume("|")) {
      size_t End = Text.find('|', Position);
      if (End == StringRef::npos)
        return fail("unterminated quoted variable name");
      Name = Text.slice(Position, End);
      Position = End + 1;
      if (WasQuoted)
        *WasQuoted = true;
      return true;
    }
    if (Position == Text.size() || !isIdentifierStart(Text[Position]))
      return fail("expected variable or constant");
    while (Position != Text.size() && isIdentifierChar(Text[Position]))
      ++Position;
    Name = Text.slice(Start, Position);
    return true;
  }

  bool parseBuiltinName(StringRef &Name) {
    skipSpace();
    assert(Text[Position] == '{');
    size_t End = Text.find('}', ++Position);
    if (End == StringRef::npos)
      return fail("unterminated built-in variable name");
    Name = Text.slice(Position, End);
    Position = End + 1;
    return true;
  }

  bool parseEscape(char &Value) {
    if (Position == Text.size())
      return fail("incomplete escape sequence");
    char C = Text[Position++];
    switch (C) {
    case 'a':
      Value = '\a';
      return true;
    case 'b':
      Value = '\b';
      return true;
    case 'f':
      Value = '\f';
      return true;
    case 'n':
      Value = '\n';
      return true;
    case 'r':
      Value = '\r';
      return true;
    case 't':
      Value = '\t';
      return true;
    case 'v':
      Value = '\v';
      return true;
    case 'x': {
      if (Position == Text.size() || !isHexDigit(Text[Position]))
        return fail("hexadecimal escape requires at least one digit");
      unsigned Result = 0;
      while (Position != Text.size() && isHexDigit(Text[Position])) {
        Result = Result * 16 + hexDigitValue(Text[Position++]);
        if (Result > 255)
          return fail("hexadecimal escape is outside the byte range");
      }
      Value = static_cast<char>(Result);
      return true;
    }
    case '\\':
    case '\'':
    case '"':
    case '?':
      Value = C;
      return true;
    default:
      if (C < '0' || C > '7') {
        Value = C;
        return true;
      }
      unsigned Result = C - '0';
      for (unsigned I = 1; I != 3 && Position != Text.size() &&
                           Text[Position] >= '0' && Text[Position] <= '7';
           ++I)
        Result = Result * 8 + Text[Position++] - '0';
      Value = static_cast<char>(Result);
      return true;
    }
  }

  bool parseString(VariableValue &Result) {
    assert(Text[Position] == '"');
    ++Position;
    std::string Value;
    while (Position != Text.size()) {
      char C = Text[Position++];
      if (C == '"') {
        if (Position != Text.size() && Text[Position] == '"') {
          Value.push_back('"');
          ++Position;
          continue;
        }
        Result = VariableValue::string(std::move(Value));
        return true;
      }
      if (C == '$') {
        if (Position == Text.size() || Text[Position] != '$')
          return fail("dollar character in string must be doubled");
        Value.push_back('$');
        ++Position;
        continue;
      }
      if (C == '\\' && !NoEscape) {
        if (!parseEscape(C))
          return false;
      }
      Value.push_back(C);
    }
    return fail("unterminated string literal");
  }

  bool parseNumber(VariableValue &Result) {
    size_t Start = Position;
    unsigned Radix = 10;
    if (consume("&")) {
      Start = Position;
      Radix = 16;
      while (Position != Text.size() && isHexDigit(Text[Position]))
        ++Position;
    } else if (Text.drop_front(Position).starts_with_insensitive("0x")) {
      Position += 2;
      Start = Position;
      Radix = 16;
      while (Position != Text.size() && isHexDigit(Text[Position]))
        ++Position;
    } else if (Position + 1 < Text.size() && Text[Position] >= '2' &&
               Text[Position] <= '9' && Text[Position + 1] == '_') {
      Radix = Text[Position] - '0';
      Position += 2;
      Start = Position;
      while (Position != Text.size() && isAlnum(Text[Position]))
        ++Position;
    } else {
      while (Position != Text.size() && isDigit(Text[Position]))
        ++Position;
    }

    StringRef Digits = Text.slice(Start, Position);
    uint64_t Value;
    if (Digits.empty() || Digits.getAsInteger(Radix, Value))
      return fail("invalid 64-bit numeric literal");
    Result = VariableValue::arithmetic(Value);
    return true;
  }

  /// %[F|B][A|T]n[routname]: a numeric local label reference.
  bool tryParseNumericLabelReference(VariableValue &Result, bool &Matched) {
    Matched = false;
    size_t Start = Position;
    if (Text[Position] != '%')
      return true;
    size_t End = Position + 1;
    char Direction = '\0';
    if (End != Text.size() &&
        (toUpper(Text[End]) == 'F' || toUpper(Text[End]) == 'B'))
      Direction = toLower(Text[End++]);
    if (End != Text.size() &&
        (toUpper(Text[End]) == 'A' || toUpper(Text[End]) == 'T'))
      ++End;
    size_t NumberStart = End;
    while (End != Text.size() && isDigit(Text[End]))
      ++End;
    if (NumberStart == End)
      return true;
    Matched = true;
    unsigned Number;
    if (Text.slice(NumberStart, End).getAsInteger(10, Number) || Number > 99)
      return fail("numeric local label must be in range 0-99");
    size_t NameStart = End;
    while (End != Text.size() && (isAlnum(Text[End]) || Text[End] == '_'))
      ++End;
    StringRef RoutName = Text.slice(NameStart, End);
    Position = End;
    if (!AllowSymbolic) {
      Position = Start;
      return fail("unknown variable or constant '" +
                  Text.slice(Start, End) + "'");
    }
    std::string Error;
    const MCExpr *Expr =
        Context.getNumericLabelExpr(Direction, Number, RoutName, Error);
    if (!Expr)
      return fail(Error);
    Result = VariableValue::symbolic(Expr);
    return true;
  }

  bool parsePrimary(VariableValue &Result) {
    skipSpace();
    if (Position == Text.size())
      return fail("expected expression");
    if (consume("(")) {
      if (!parseExpression(Result, 1))
        return false;
      skipSpace();
      if (!consume(")"))
        return fail("expected ')' in expression");
      return true;
    }
    if (Text[Position] == '"')
      return parseString(Result);
    if (Text[Position] == '{') {
      StringRef Name;
      if (!parseBuiltinName(Name))
        return false;
      if (Name.equals_insensitive("PC")) {
        if (AllowSymbolic)
          if (const MCExpr *Expr = Context.getLocationExpr(/*IsPC=*/true)) {
            Result = VariableValue::symbolic(Expr);
            return true;
          }
        return fail("{PC} cannot be used in an assembly-time expression");
      }
      if (std::optional<VariableValue> Value =
              getBuiltinVariable(Name, Context.getBuiltins())) {
        Result = std::move(*Value);
        return true;
      }
      return fail("unknown built-in variable '{" + Name + "}'");
    }
    if (Text[Position] == '\'') {
      ++Position;
      if (Position == Text.size())
        return fail("unterminated character literal");
      char Value = Text[Position++];
      if (Value == '\\' && !NoEscape && !parseEscape(Value))
        return false;
      if (Position == Text.size() || Text[Position++] != '\'')
        return fail("character literal must contain one character");
      Result = VariableValue::arithmetic(static_cast<unsigned char>(Value));
      return true;
    }
    if (isDigit(Text[Position]) ||
        (Text[Position] == '&' && Position + 1 != Text.size() &&
         isHexDigit(Text[Position + 1])))
      return parseNumber(Result);
    if (Text[Position] == '%') {
      bool Matched;
      if (!tryParseNumericLabelReference(Result, Matched))
        return false;
      if (Matched)
        return true;
    }
    if (AllowSymbolic && Text[Position] == '.' &&
        (Position + 1 == Text.size() || !isIdentifierChar(Text[Position + 1]))) {
      if (const MCExpr *Expr = Context.getLocationExpr(/*IsPC=*/false)) {
        ++Position;
        Result = VariableValue::symbolic(Expr);
        return true;
      }
    }

    StringRef Name;
    bool WasQuoted;
    if (!parseIdentifier(Name, &WasQuoted))
      return false;
    if (const VariableValue *Variable = Variables.find(Name)) {
      Result = *Variable;
      return true;
    }
    if (std::optional<uint64_t> Value = Context.lookUpConstant(Name)) {
      Result = VariableValue::arithmetic(*Value);
      return true;
    }
    if (std::optional<VariableValue> Value =
            Context.lookUpRegisterRelative(Name)) {
      Result = *Value;
      return true;
    }
    if (AllowSymbolic)
      if (const MCExpr *Expr = Context.getSymbolExpr(Name)) {
        Result = VariableValue::symbolic(Expr);
        return true;
      }
    return fail("unknown variable or constant '" + Name + "'");
  }

  bool parseUnary(VariableValue &Result) {
    skipSpace();
    if (consume("+")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind == VariableKind::Symbolic)
        return true;
      return Result.Kind == VariableKind::Arithmetic ||
             fail("unary '+' requires a numeric expression");
    }
    if (consume("-")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind == VariableKind::Symbolic) {
        Result.Expr = MCUnaryExpr::createMinus(Result.Expr, ctx());
        return true;
      }
      if (Result.Kind != VariableKind::Arithmetic)
        return fail("unary '-' requires a numeric expression");
      Result.Arithmetic = 0U - Result.Arithmetic;
      return true;
    }
    if (consume("~") || consumeInsensitive(":NOT:")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind == VariableKind::Symbolic) {
        Result.Expr = MCUnaryExpr::createNot(Result.Expr, ctx());
        return true;
      }
      if (Result.Kind != VariableKind::Arithmetic)
        return fail(":NOT: requires a numeric expression");
      Result.Arithmetic = ~Result.Arithmetic;
      return true;
    }
    if (consumeInsensitive(":LNOT:")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind != VariableKind::Logical)
        return fail(":LNOT: requires a logical expression");
      Result.Logical = !Result.Logical;
      return true;
    }
    if (Text.drop_front(Position).starts_with("!") &&
        !Text.drop_front(Position).starts_with("!=")) {
      ++Position;
      if (!parseUnary(Result))
        return false;
      if (Result.Kind != VariableKind::Logical)
        return fail("'!' requires a logical expression");
      Result.Logical = !Result.Logical;
      return true;
    }
    bool IsBase = consumeInsensitive(":BASE:");
    if (IsBase || consumeInsensitive(":INDEX:")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind != VariableKind::RegisterRelative)
        return fail(IsBase ? "wrong operand type for :BASE:"
                           : "wrong operand type for :INDEX:");
      Result = VariableValue::arithmetic(IsBase ? Result.RegisterBase
                                                : Result.Arithmetic);
      return true;
    }
    if (Position != Text.size() && Text[Position] == '?') {
      ++Position;
      StringRef Name;
      if (!parseIdentifier(Name))
        return false;
      if (std::optional<uint64_t> Size = Context.lookUpSymbolSize(Name)) {
        Result = VariableValue::arithmetic(*Size);
        return true;
      }
      if (AllowSymbolic)
        if (const MCExpr *Expr = Context.getSymbolSizeExpr(Name)) {
          Result = VariableValue::symbolic(Expr);
          return true;
        }
      return fail("unknown symbol '" + Name + "'");
    }
    if (consumeInsensitive(":RCONST:")) {
      StringRef Name;
      if (!parseIdentifier(Name))
        return false;
      unsigned Register;
      std::optional<unsigned> DialectRegister;
      if (Context.lookUpRegisterConstant(Name, DialectRegister)) {
        if (!DialectRegister)
          return fail(":RCONST: requires a register");
        Result = VariableValue::arithmetic(*DialectRegister);
        return true;
      }
      if (Name.equals_insensitive("fp"))
        Register = 29;
      else if (Name.equals_insensitive("lr"))
        Register = 30;
      else if (Name.equals_insensitive("sp") ||
               Name.equals_insensitive("wsp") ||
               Name.equals_insensitive("xzr") || Name.equals_insensitive("wzr"))
        Register = 31;
      else {
        StringRef Number = Name;
        if ((!Number.consume_front_insensitive("x") &&
             !Number.consume_front_insensitive("w")) ||
            Number.getAsInteger(10, Register) || Register > 30)
          return fail(":RCONST: requires a general-purpose register");
      }
      Result = VariableValue::arithmetic(Register);
      return true;
    }
    if (consumeInsensitive(":STR:")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind == VariableKind::Arithmetic)
        Result = VariableValue::string(utohexstr(Result.Arithmetic, false, 8));
      else if (Result.Kind == VariableKind::Logical)
        Result = VariableValue::string(Result.Logical ? "T" : "F");
      else
        return fail(":STR: requires a numeric or logical expression");
      return true;
    }
    if (consumeInsensitive(":CHR:")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind != VariableKind::Arithmetic || Result.Arithmetic > 255)
        return fail(":CHR: requires a numeric value from 0 to 255");
      Result = VariableValue::string(
          std::string(1, static_cast<char>(Result.Arithmetic)));
      return true;
    }
    if (consumeInsensitive(":LEN:")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind != VariableKind::String)
        return fail(":LEN: requires a string expression");
      Result = VariableValue::arithmetic(Result.String.size());
      return true;
    }
    bool Lower = consumeInsensitive(":LOWERCASE:");
    if (Lower || consumeInsensitive(":UPPERCASE:")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind != VariableKind::String)
        return fail("case conversion requires a string expression");
      for (char &C : Result.String)
        C = Lower ? toLower(C) : toUpper(C);
      return true;
    }
    if (consumeInsensitive(":DEF:")) {
      skipSpace();
      if (Position != Text.size() && Text[Position] == '{') {
        StringRef Name;
        if (!parseBuiltinName(Name))
          return false;
        if (Name.equals_insensitive("PC"))
          return fail("{PC} cannot be used in an assembly-time expression");
        Result = VariableValue::logical(
            isBuiltinVariable(Name, Context.getBuiltins()));
        return true;
      }
      StringRef Name;
      std::string SavedError = std::move(ErrorMessage);
      if (!parseIdentifier(Name))
        return false;
      ErrorMessage = std::move(SavedError);
      Result = VariableValue::logical(Variables.contains(Name) ||
                                      Context.lookUpConstant(Name) ||
                                      Context.isSymbolDefined(Name));
      return true;
    }
    return parsePrimary(Result);
  }

  std::optional<ParsedOperator> parseBinaryOperator() {
    skipSpace();
    struct OperatorSpelling {
      StringLiteral Spelling;
      BinaryOperator Operator;
      unsigned Precedence;
    };
    static constexpr OperatorSpelling Operators[] = {
        {":LEOR:", BinaryOperator::LogicalExclusiveOr, 1},
        {":LAND:", BinaryOperator::LogicalAnd, 1},
        {":LOR:", BinaryOperator::LogicalOr, 1},
        {":LEFT:", BinaryOperator::Left, 5},
        {":RIGHT:", BinaryOperator::Right, 5},
        {":ROL:", BinaryOperator::RotateLeft, 4},
        {":ROR:", BinaryOperator::RotateRight, 4},
        {":SHL:", BinaryOperator::ShiftLeft, 4},
        {":SHR:", BinaryOperator::ShiftRight, 4},
        {":MOD:", BinaryOperator::Modulo, 6},
        {":AND:", BinaryOperator::And, 3},
        {":EOR:", BinaryOperator::ExclusiveOr, 3},
        {":OR:", BinaryOperator::Or, 3},
        {":CC:", BinaryOperator::Concatenate, 5},
    };
    for (const OperatorSpelling &Entry : Operators)
      if (consumeInsensitive(Entry.Spelling))
        return ParsedOperator{Entry.Operator, Entry.Precedence};

    static constexpr OperatorSpelling Symbols[] = {
        {">=", BinaryOperator::GreaterEqual, 2},
        {"<=", BinaryOperator::LessEqual, 2},
        {"==", BinaryOperator::Equal, 2},
        {"/=", BinaryOperator::NotEqual, 2},
        {"<>", BinaryOperator::NotEqual, 2},
        {"!=", BinaryOperator::NotEqual, 2},
        {"<<", BinaryOperator::ShiftLeft, 4},
        {">>", BinaryOperator::ShiftRight, 4},
        {"&&", BinaryOperator::LogicalAnd, 1},
        {"||", BinaryOperator::LogicalOr, 1},
        {"*", BinaryOperator::Multiply, 6},
        {"/", BinaryOperator::Divide, 6},
        {"%", BinaryOperator::Modulo, 6},
        {"+", BinaryOperator::Add, 3},
        {"-", BinaryOperator::Subtract, 3},
        {"&", BinaryOperator::And, 3},
        {"^", BinaryOperator::ExclusiveOr, 3},
        {"|", BinaryOperator::Or, 3},
        {"=", BinaryOperator::Equal, 2},
        {">", BinaryOperator::Greater, 2},
        {"<", BinaryOperator::Less, 2},
    };
    for (const OperatorSpelling &Entry : Symbols)
      if (consume(Entry.Spelling))
        return ParsedOperator{Entry.Operator, Entry.Precedence};
    return std::nullopt;
  }

  bool requireKinds(const VariableValue &Left, const VariableValue &Right,
                    VariableKind Kind, StringRef Description) {
    return (Left.Kind == Kind && Right.Kind == Kind) ||
           fail(Description + " requires operands of the same type");
  }

  MCContext &ctx() {
    MCContext *Ctx = Context.getContext();
    assert(Ctx && "symbolic expressions require an MCContext");
    return *Ctx;
  }

  const MCExpr *toExpr(const VariableValue &Value) {
    if (Value.Kind == VariableKind::Symbolic)
      return Value.Expr;
    return MCConstantExpr::create(static_cast<int64_t>(Value.Arithmetic),
                                  ctx());
  }

  /// Combine operands when at least one is symbolic.
  bool applySymbolic(VariableValue &Left, BinaryOperator Operator,
                     const VariableValue &Right) {
    auto IsNumeric = [](const VariableValue &Value) {
      return Value.Kind == VariableKind::Arithmetic ||
             Value.Kind == VariableKind::Symbolic;
    };
    if (!IsNumeric(Left) || !IsNumeric(Right))
      return fail("numeric operator requires operands of the same type");
    MCBinaryExpr::Opcode Opcode;
    switch (Operator) {
    case BinaryOperator::Multiply:
      Opcode = MCBinaryExpr::Mul;
      break;
    case BinaryOperator::Divide:
      Opcode = MCBinaryExpr::Div;
      break;
    case BinaryOperator::Modulo:
      Opcode = MCBinaryExpr::Mod;
      break;
    case BinaryOperator::ShiftLeft:
      Opcode = MCBinaryExpr::Shl;
      break;
    case BinaryOperator::ShiftRight:
      Opcode = MCBinaryExpr::LShr;
      break;
    case BinaryOperator::Add:
      Opcode = MCBinaryExpr::Add;
      break;
    case BinaryOperator::Subtract:
      Opcode = MCBinaryExpr::Sub;
      break;
    case BinaryOperator::And:
      Opcode = MCBinaryExpr::And;
      break;
    case BinaryOperator::Or:
      Opcode = MCBinaryExpr::Or;
      break;
    case BinaryOperator::ExclusiveOr:
      Opcode = MCBinaryExpr::Xor;
      break;
    case BinaryOperator::Equal:
      Opcode = MCBinaryExpr::EQ;
      break;
    case BinaryOperator::NotEqual:
      Opcode = MCBinaryExpr::NE;
      break;
    case BinaryOperator::Greater:
      Opcode = MCBinaryExpr::GT;
      break;
    case BinaryOperator::GreaterEqual:
      Opcode = MCBinaryExpr::GTE;
      break;
    case BinaryOperator::Less:
      Opcode = MCBinaryExpr::LT;
      break;
    case BinaryOperator::LessEqual:
      Opcode = MCBinaryExpr::LTE;
      break;
    default:
      return fail("operator cannot be applied to a relocatable value");
    }
    Left = VariableValue::symbolic(
        MCBinaryExpr::create(Opcode, toExpr(Left), toExpr(Right), ctx()));
    return true;
  }

  bool applyBinaryOperator(VariableValue &Left, BinaryOperator Operator,
                           const VariableValue &Right) {
    if (Left.Kind == VariableKind::Symbolic ||
        Right.Kind == VariableKind::Symbolic)
      return applySymbolic(Left, Operator, Right);
    auto Numeric = [&]() {
      return requireKinds(Left, Right, VariableKind::Arithmetic,
                          "numeric operator");
    };
    auto Logical = [&]() {
      return requireKinds(Left, Right, VariableKind::Logical,
                          "Boolean operator");
    };
    switch (Operator) {
    case BinaryOperator::Multiply:
      if (!Numeric())
        return false;
      Left.Arithmetic *= Right.Arithmetic;
      return true;
    case BinaryOperator::Divide:
      if (!Numeric())
        return false;
      if (!Right.Arithmetic)
        return fail("division by zero");
      Left.Arithmetic = APInt(64, Left.Arithmetic)
                            .sdiv(APInt(64, Right.Arithmetic))
                            .getZExtValue();
      return true;
    case BinaryOperator::Modulo:
      if (!Numeric())
        return false;
      if (!Right.Arithmetic)
        return fail("division by zero");
      Left.Arithmetic = APInt(64, Left.Arithmetic)
                            .srem(APInt(64, Right.Arithmetic))
                            .getZExtValue();
      return true;
    case BinaryOperator::Concatenate:
      if (!requireKinds(Left, Right, VariableKind::String,
                        "string concatenation"))
        return false;
      Left.String += Right.String;
      return true;
    case BinaryOperator::Left:
    case BinaryOperator::Right: {
      if (Left.Kind != VariableKind::String ||
          Right.Kind != VariableKind::Arithmetic)
        return fail("string slicing requires a string and a numeric length");
      size_t Length = std::min<size_t>(Right.Arithmetic, Left.String.size());
      if (Operator == BinaryOperator::Left)
        Left.String.resize(Length);
      else
        Left.String = Left.String.substr(Left.String.size() - Length);
      return true;
    }
    case BinaryOperator::RotateLeft:
    case BinaryOperator::RotateRight: {
      if (!Numeric())
        return false;
      unsigned Amount = Right.Arithmetic & 31;
      uint32_t Value = Left.Arithmetic;
      if (Operator == BinaryOperator::RotateLeft)
        Value = (Value << Amount) | (Value >> ((32 - Amount) & 31));
      else
        Value = (Value >> Amount) | (Value << ((32 - Amount) & 31));
      Left.Arithmetic = Value;
      return true;
    }
    case BinaryOperator::ShiftLeft:
    case BinaryOperator::ShiftRight: {
      if (!Numeric())
        return false;
      if (Right.Arithmetic >= 64)
        Left.Arithmetic = 0;
      else if (Operator == BinaryOperator::ShiftLeft)
        Left.Arithmetic <<= Right.Arithmetic;
      else
        Left.Arithmetic >>= Right.Arithmetic;
      return true;
    }
    case BinaryOperator::Add:
      if (Left.Kind == VariableKind::RegisterRelative &&
          Right.Kind == VariableKind::Arithmetic) {
        Left.Arithmetic += Right.Arithmetic;
        return true;
      }
      if (Left.Kind == VariableKind::Arithmetic &&
          Right.Kind == VariableKind::RegisterRelative) {
        Left = VariableValue::registerRelative(
            Right.RegisterBase, Left.Arithmetic + Right.Arithmetic);
        return true;
      }
      if (!Numeric())
        return false;
      Left.Arithmetic += Right.Arithmetic;
      return true;
    case BinaryOperator::Subtract:
      if (Left.Kind == VariableKind::RegisterRelative &&
          Right.Kind == VariableKind::Arithmetic) {
        Left.Arithmetic -= Right.Arithmetic;
        return true;
      }
      if (Left.Kind == VariableKind::RegisterRelative &&
          Right.Kind == VariableKind::RegisterRelative &&
          Left.RegisterBase == Right.RegisterBase) {
        Left = VariableValue::arithmetic(Left.Arithmetic - Right.Arithmetic);
        return true;
      }
      if (!Numeric())
        return false;
      Left.Arithmetic -= Right.Arithmetic;
      return true;
    case BinaryOperator::And:
      if (!Numeric())
        return false;
      Left.Arithmetic &= Right.Arithmetic;
      return true;
    case BinaryOperator::Or:
      if (!Numeric())
        return false;
      Left.Arithmetic |= Right.Arithmetic;
      return true;
    case BinaryOperator::ExclusiveOr:
      if (!Numeric())
        return false;
      Left.Arithmetic ^= Right.Arithmetic;
      return true;
    case BinaryOperator::Equal:
    case BinaryOperator::NotEqual: {
      if (Left.Kind != Right.Kind)
        return fail("comparison requires operands of the same type");
      bool Equal = Left.Kind == VariableKind::Arithmetic
                       ? Left.Arithmetic == Right.Arithmetic
                   : Left.Kind == VariableKind::Logical
                       ? Left.Logical == Right.Logical
                       : Left.String == Right.String;
      Left = VariableValue::logical(Operator == BinaryOperator::Equal ? Equal
                                                                      : !Equal);
      return true;
    }
    case BinaryOperator::Greater:
    case BinaryOperator::GreaterEqual:
    case BinaryOperator::Less:
    case BinaryOperator::LessEqual: {
      if (Left.Kind != Right.Kind || Left.Kind == VariableKind::Logical)
        return fail("ordered comparison requires matching numeric or string "
                    "operands");
      int Comparison = Left.Kind == VariableKind::Arithmetic
                           ? (Left.Arithmetic < Right.Arithmetic
                                  ? -1
                                  : Left.Arithmetic != Right.Arithmetic)
                           : Left.String.compare(Right.String);
      bool Value = Operator == BinaryOperator::Greater        ? Comparison > 0
                   : Operator == BinaryOperator::GreaterEqual ? Comparison >= 0
                   : Operator == BinaryOperator::Less         ? Comparison < 0
                                                              : Comparison <= 0;
      Left = VariableValue::logical(Value);
      return true;
    }
    case BinaryOperator::LogicalAnd:
      if (!Logical())
        return false;
      Left.Logical &= Right.Logical;
      return true;
    case BinaryOperator::LogicalOr:
      if (!Logical())
        return false;
      Left.Logical |= Right.Logical;
      return true;
    case BinaryOperator::LogicalExclusiveOr:
      if (!Logical())
        return false;
      Left.Logical ^= Right.Logical;
      return true;
    }
    llvm_unreachable("unknown ARMASM binary operator");
  }

  /// In partial mode an operator is only taken if an operand can follow it;
  /// "x0, #8]" must stop before "]" and "[x1]!" before "!".
  bool parseExpression(VariableValue &Result, unsigned MinimumPrecedence) {
    if (!parseUnary(Result))
      return false;
    while (true) {
      size_t OperatorPosition = Position;
      std::optional<ParsedOperator> Operator = parseBinaryOperator();
      if (!Operator || Operator->Precedence < MinimumPrecedence) {
        Position = OperatorPosition;
        return true;
      }
      VariableValue Right;
      if (!parseExpression(Right, Operator->Precedence + 1) ||
          !applyBinaryOperator(Result, Operator->Operator, Right))
        return false;
    }
  }

public:
  ExpressionParser(StringRef Text, VariableLookup Variables,
                   ExpressionContext &Context, bool NoEscape,
                   bool AllowSymbolic = false, bool Partial = false)
      : Text(Text), Variables(Variables), Context(Context), NoEscape(NoEscape),
        AllowSymbolic(AllowSymbolic), Partial(Partial) {}

  Expected<VariableValue> parse() {
    VariableValue Result;
    if (!parseExpression(Result, 1))
      return createStringError(inconvertibleErrorCode(), ErrorMessage);
    skipSpace();
    if (!Partial && Position != Text.size())
      return createStringError(inconvertibleErrorCode(),
                               "unexpected token in expression: " +
                                   Text.drop_front(Position));
    return Result;
  }

  /// Parse a single unary/primary operand (for parsePrimaryExpr).
  Expected<VariableValue> parseOperand() {
    VariableValue Result;
    if (!parseUnary(Result))
      return createStringError(inconvertibleErrorCode(), ErrorMessage);
    return Result;
  }

  size_t getPosition() const { return Position; }
};

//===----------------------------------------------------------------------===//
// Source text helpers
//===----------------------------------------------------------------------===//

StringRef takeToken(StringRef &Text) {
  Text = Text.ltrim();
  if (Text.starts_with("|")) {
    size_t End =
        Text.starts_with("||") ? Text.find("||", 2) : Text.find('|', 1);
    if (End != StringRef::npos) {
      End += Text.starts_with("||") ? 2 : 1;
      StringRef Token = Text.take_front(End);
      Text = Text.drop_front(End).ltrim();
      return Token;
    }
  }
  size_t End = Text.find_first_of(" \t");
  StringRef Token = Text.take_front(End);
  Text = End == StringRef::npos ? StringRef() : Text.drop_front(End).ltrim();
  return Token;
}

StringRef unquoteIdentifier(StringRef Name) {
  Name = Name.trim();
  if (Name.starts_with("||") && Name.ends_with("||"))
    return Name.drop_front(2).drop_back(2);
  if (Name.starts_with("|") && Name.ends_with("|"))
    return Name.drop_front().drop_back();
  return Name;
}

/// Strip a ';' comment, respecting strings, character literals and |quoted|
/// names.
StringRef stripComment(StringRef Line) {
  char Quote = '\0';
  bool Escaped = false;
  for (size_t I = 0; I != Line.size(); ++I) {
    char C = Line[I];
    if (Escaped) {
      Escaped = false;
      continue;
    }
    if (Quote) {
      if (C == '\\' && Quote != '|')
        Escaped = true;
      else if (C == Quote) {
        if (Quote == '"' && I + 1 != Line.size() && Line[I + 1] == '"')
          ++I;
        else
          Quote = '\0';
      }
      continue;
    }
    if (C == '"' || C == '\'' || C == '|')
      Quote = C;
    else if (C == ';')
      return Line.take_front(I);
  }
  return Line;
}

void splitOperands(StringRef Text, SmallVectorImpl<StringRef> &Operands) {
  char Quote = '\0';
  bool Escaped = false;
  size_t Start = 0;
  for (size_t I = 0; I != Text.size(); ++I) {
    char C = Text[I];
    if (Escaped) {
      Escaped = false;
      continue;
    }
    if (Quote) {
      if (C == '\\' && Quote != '|')
        Escaped = true;
      else if (C == Quote) {
        if (Quote == '"' && I + 1 != Text.size() && Text[I + 1] == '"')
          ++I;
        else
          Quote = '\0';
      }
      continue;
    }
    if (C == '|' && Text.slice(Start, I).trim().empty() &&
        (I + 1 == Text.size() || Text[I + 1] == ','))
      continue;
    if (C == '"' || C == '\'' || C == '|')
      Quote = C;
    else if (C == ',') {
      Operands.push_back(Text.slice(Start, I).trim());
      Start = I + 1;
    }
  }
  Operands.push_back(Text.drop_front(Start).trim());
}

std::string formatSubstitution(const VariableValue &Variable) {
  switch (Variable.Kind) {
  case VariableKind::Arithmetic:
    return utohexstr(Variable.Arithmetic, false, 8);
  case VariableKind::Logical:
    return Variable.Logical ? "T" : "F";
  case VariableKind::String:
    return Variable.String;
  case VariableKind::RegisterRelative:
  case VariableKind::Symbolic:
    break;
  }
  llvm_unreachable("value cannot be substituted");
}

/// Replace $name and $name. with the value of an assembly-time variable.
std::string substituteVariables(StringRef Line, VariableLookup Variables) {
  if (!Line.contains('$'))
    return Line.str();
  std::string Substituted;
  raw_string_ostream OS(Substituted);
  bool InString = false;
  bool InBars = false;
  for (size_t I = 0; I != Line.size();) {
    char C = Line[I];
    if (C == '"') {
      OS << C;
      if (InString && I + 1 != Line.size() && Line[I + 1] == '"') {
        OS << '"';
        I += 2;
        continue;
      }
      InString = !InString;
      ++I;
      continue;
    }
    if (C == '|' && !InString) {
      InBars = !InBars;
      OS << C;
      ++I;
      continue;
    }
    if (C != '$' || InBars || (I + 1 != Line.size() && Line[I + 1] == '$')) {
      OS << C;
      if (C == '$' && I + 1 != Line.size() && Line[I + 1] == '$') {
        OS << '$';
        I += 2;
      } else {
        ++I;
      }
      continue;
    }

    size_t NameStart = I + 1;
    size_t NameEnd = NameStart;
    if (NameEnd != Line.size() &&
        (isAlpha(Line[NameEnd]) || Line[NameEnd] == '_')) {
      ++NameEnd;
      while (NameEnd != Line.size() &&
             (isAlnum(Line[NameEnd]) || Line[NameEnd] == '_'))
        ++NameEnd;
    }
    const VariableValue *Variable =
        NameEnd == NameStart ? nullptr
                             : Variables.find(Line.slice(NameStart, NameEnd));
    if (!Variable || Variable->Kind == VariableKind::RegisterRelative ||
        Variable->Kind == VariableKind::Symbolic) {
      OS << C;
      ++I;
      continue;
    }
    OS << formatSubstitution(*Variable);
    I = NameEnd;
    if (I != Line.size() && Line[I] == '.')
      ++I;
  }
  return Substituted;
}

std::string substituteMacroParameters(StringRef Line,
                                      const StringMap<std::string> &Parameters) {
  std::string Substituted;
  raw_string_ostream OS(Substituted);
  bool InString = false;
  bool InBars = false;
  for (size_t I = 0; I != Line.size();) {
    char C = Line[I];
    if (C == '"') {
      OS << C;
      if (InString && I + 1 != Line.size() && Line[I + 1] == '"') {
        OS << '"';
        I += 2;
        continue;
      }
      InString = !InString;
      ++I;
      continue;
    }
    if (C == '|' && !InString) {
      InBars = !InBars;
      OS << C;
      ++I;
      continue;
    }
    if (C != '$' || InBars || (I + 1 != Line.size() && Line[I + 1] == '$')) {
      OS << C;
      if (C == '$' && I + 1 != Line.size() && Line[I + 1] == '$') {
        OS << '$';
        I += 2;
      } else {
        ++I;
      }
      continue;
    }

    size_t NameStart = I + 1;
    size_t NameEnd = NameStart;
    if (NameEnd != Line.size() &&
        (isAlpha(Line[NameEnd]) || Line[NameEnd] == '_')) {
      ++NameEnd;
      while (NameEnd != Line.size() &&
             (isAlnum(Line[NameEnd]) || Line[NameEnd] == '_'))
        ++NameEnd;
    }
    auto It = Parameters.find(Line.slice(NameStart, NameEnd));
    if (NameEnd == NameStart || It == Parameters.end()) {
      OS << C;
      ++I;
      continue;
    }
    OS << It->second;
    I = NameEnd;
    if (I != Line.size() && Line[I] == '.')
      ++I;
  }
  return Substituted;
}

std::string normalizeMacroArgument(StringRef Argument) {
  Argument = Argument.trim();
  if (Argument.size() < 2 ||
      ((Argument.front() != '"' || Argument.back() != '"') &&
       (Argument.front() != '\'' || Argument.back() != '\'')))
    return Argument.str();

  char Quote = Argument.front();
  Argument = Argument.drop_front().drop_back();
  std::string Value;
  for (size_t I = 0; I != Argument.size(); ++I) {
    if (Argument[I] == Quote && I + 1 != Argument.size() &&
        Argument[I + 1] == Quote)
      ++I;
    Value.push_back(Argument[I]);
  }
  return Value;
}

bool isValidVariableName(StringRef Name) {
  if (Name.size() >= 2 && Name.starts_with("|") && Name.ends_with("|"))
    return Name.size() != 2;
  if (Name.empty() || (!isAlpha(Name.front()) && Name.front() != '_'))
    return false;
  return llvm::all_of(Name.drop_front(),
                      [](char C) { return isAlnum(C) || C == '_'; });
}

bool isSetDirective(StringRef Directive) {
  return Directive.equals_insensitive("SETA") ||
         Directive.equals_insensitive("SETL") ||
         Directive.equals_insensitive("SETS");
}

bool isEquDirective(StringRef Directive) {
  return Directive.equals_insensitive("EQU") || Directive == "*";
}

bool isBranchMnemonic(StringRef Token) {
  return Token.equals_insensitive("B") || Token.equals_insensitive("BL");
}

bool isStorageDirective(StringRef Token) {
  return Token.equals_insensitive("SPACE") ||
         Token.equals_insensitive("FILL") || Token == "%";
}

enum class DataDirectiveKind { Byte, Word, Long, Quad, Single, Double };

struct DataDirective {
  DataDirectiveKind Kind;
  bool Unaligned = false;
  bool MicrosoftCompatible = true;

  bool isFloatingPoint() const {
    return Kind == DataDirectiveKind::Single ||
           Kind == DataDirectiveKind::Double;
  }

  int64_t minimumValue() const {
    switch (Kind) {
    case DataDirectiveKind::Byte:
      return -128;
    case DataDirectiveKind::Word:
      return -32768;
    case DataDirectiveKind::Long:
      return INT32_MIN;
    case DataDirectiveKind::Quad:
      return INT64_MIN;
    case DataDirectiveKind::Single:
    case DataDirectiveKind::Double:
      break;
    }
    llvm_unreachable("floating-point directive has no integer range");
  }

  uint64_t maximumValue() const {
    switch (Kind) {
    case DataDirectiveKind::Byte:
      return UINT8_MAX;
    case DataDirectiveKind::Word:
      return UINT16_MAX;
    case DataDirectiveKind::Long:
      return UINT32_MAX;
    case DataDirectiveKind::Quad:
      return UINT64_MAX;
    case DataDirectiveKind::Single:
    case DataDirectiveKind::Double:
      break;
    }
    llvm_unreachable("floating-point directive has no integer range");
  }

  unsigned size() const {
    switch (Kind) {
    case DataDirectiveKind::Byte:
      return 1;
    case DataDirectiveKind::Word:
      return 2;
    case DataDirectiveKind::Long:
    case DataDirectiveKind::Single:
      return 4;
    case DataDirectiveKind::Quad:
    case DataDirectiveKind::Double:
      return 8;
    }
    llvm_unreachable("unknown data directive kind");
  }

  unsigned alignment() const { return std::min(size(), 4U); }
};

/// The CodeView basic type armasm records for data defined by \p Data.
unsigned getCodeViewDataType(const DataDirective &Data) {
  switch (Data.Kind) {
  case DataDirectiveKind::Byte:
    return static_cast<unsigned>(codeview::SimpleTypeKind::UnsignedCharacter);
  case DataDirectiveKind::Word:
    return static_cast<unsigned>(codeview::SimpleTypeKind::UInt16);
  case DataDirectiveKind::Long:
    return static_cast<unsigned>(codeview::SimpleTypeKind::UInt32);
  case DataDirectiveKind::Quad:
    return static_cast<unsigned>(codeview::SimpleTypeKind::UInt64);
  case DataDirectiveKind::Single:
    return static_cast<unsigned>(codeview::SimpleTypeKind::Float32);
  case DataDirectiveKind::Double:
    return static_cast<unsigned>(codeview::SimpleTypeKind::Float64);
  }
  llvm_unreachable("unknown data directive kind");
}

std::optional<DataDirective> getDataDirective(StringRef Token) {
  if (Token.equals_insensitive("DCB") || Token == "=")
    return DataDirective{DataDirectiveKind::Byte};
  if (Token.equals_insensitive("DCW"))
    return DataDirective{DataDirectiveKind::Word};
  if (Token.equals_insensitive("DCWU"))
    return DataDirective{DataDirectiveKind::Word, /*Unaligned=*/true};
  if (Token.equals_insensitive("DCD") || Token == "&")
    return DataDirective{DataDirectiveKind::Long};
  if (Token.equals_insensitive("DCDU"))
    return DataDirective{DataDirectiveKind::Long, /*Unaligned=*/true};
  if (Token.equals_insensitive("DCQ"))
    return DataDirective{DataDirectiveKind::Quad};
  if (Token.equals_insensitive("DCQU"))
    return DataDirective{DataDirectiveKind::Quad, /*Unaligned=*/true};
  if (Token.equals_insensitive("DCFS"))
    return DataDirective{DataDirectiveKind::Single};
  if (Token.equals_insensitive("DCFSU"))
    return DataDirective{DataDirectiveKind::Single, /*Unaligned=*/true};
  if (Token.equals_insensitive("DCFD"))
    return DataDirective{DataDirectiveKind::Double};
  if (Token.equals_insensitive("DCFDU"))
    return DataDirective{DataDirectiveKind::Double, /*Unaligned=*/true};
  if (Token.equals_insensitive("DCI") || Token.equals_insensitive("DCI.W"))
    return DataDirective{DataDirectiveKind::Long, /*Unaligned=*/false,
                         /*MicrosoftCompatible=*/false};
  return std::nullopt;
}

bool isDataDirective(StringRef Token) {
  return getDataDirective(Token).has_value();
}

struct AreaName {
  StringRef Name;
  StringRef ComdatSymbol;
};

Expected<AreaName> parseAreaName(StringRef Text) {
  Text = Text.trim();
  StringRef Name;
  if (Text.consume_front("|")) {
    size_t End = Text.find('|');
    if (End == StringRef::npos)
      return createStringError(inconvertibleErrorCode(),
                               "unterminated area name");
    Name = Text.take_front(End);
    Text = Text.drop_front(End + 1).trim();
  } else {
    auto [UnquotedName, Rest] = Text.split('{');
    Name = UnquotedName.trim();
    if (Name.find_first_of(" \t") != StringRef::npos)
      return createStringError(inconvertibleErrorCode(),
                               "A2003: improper line syntax: ,");
    Text = Rest.empty() ? StringRef() : Text.drop_front(UnquotedName.size());
  }
  if (Name.empty())
    return createStringError(inconvertibleErrorCode(), "expected area name");
  if (Text.empty())
    return AreaName{Name, {}};
  if (!Text.consume_front("{") || !Text.consume_back("}"))
    return createStringError(inconvertibleErrorCode(),
                             "invalid COMDAT area name");
  StringRef ComdatSymbol = unquoteIdentifier(Text.trim());
  if (ComdatSymbol.empty())
    return createStringError(inconvertibleErrorCode(),
                             "expected COMDAT symbol");
  return AreaName{Name, ComdatSymbol};
}

struct NumericLocalLabel {
  unsigned Number;
  StringRef RoutName;
};

std::optional<NumericLocalLabel> parseNumericLocalLabel(StringRef Token) {
  if (Token.empty() || !isDigit(Token.front()))
    return std::nullopt;
  size_t End = 0;
  while (End != Token.size() && isDigit(Token[End]))
    ++End;
  unsigned Number;
  if (Token.take_front(End).getAsInteger(10, Number) || Number > 99)
    return std::nullopt;
  StringRef RoutName = Token.drop_front(End);
  if (!RoutName.empty()) {
    if (!isAlpha(RoutName.front()) && RoutName.front() != '_')
      return std::nullopt;
    if (!llvm::all_of(RoutName.drop_front(),
                      [](char C) { return isAlnum(C) || C == '_'; }))
      return std::nullopt;
  }
  return NumericLocalLabel{Number, RoutName};
}

/// Whether \p Text uses {PC} outside string and character literals.
bool usesProgramCounter(StringRef Text) {
  for (size_t I = 0; I != Text.size(); ++I) {
    char C = Text[I];
    if (C == '"' || C == '\'') {
      for (++I; I != Text.size(); ++I) {
        if (Text[I] == '\\' && I + 1 != Text.size())
          ++I;
        else if (Text[I] == C) {
          if (C == '"' && I + 1 != Text.size() && Text[I + 1] == '"')
            ++I;
          else
            break;
        }
      }
      if (I == Text.size())
        return false;
      continue;
    }
    if (C == '{' && Text.drop_front(I).starts_with_insensitive("{PC}"))
      return true;
  }
  return false;
}

std::string normalizeDebugPath(StringRef Filename) {
  if (Filename == "<stdin>")
    return Filename.str();
  SmallString<256> Path(Filename);
  sys::path::native(Path);
  if (!sys::path::is_absolute(Path))
    if (sys::fs::make_absolute(Path))
      return Path.str().str();
  sys::path::remove_dots(Path, /*remove_dot_dot=*/true);
  return Path.str().str();
}

bool isIntegerValueInRange(uint64_t Value, int64_t Minimum, uint64_t Maximum) {
  int64_t SignedValue = static_cast<int64_t>(Value);
  return SignedValue < 0 ? SignedValue >= Minimum : Value <= Maximum;
}

//===----------------------------------------------------------------------===//
// Architecture traits
//===----------------------------------------------------------------------===//

/// The few properties of the dialect that differ between armasm (32-bit ARM,
/// Thumb-2) and armasm64 (AArch64). Instruction syntax itself is handled by
/// the target's MCTargetAsmParser.
class DialectTraits {
  bool Is64Bit;

public:
  explicit DialectTraits(const Triple &T) : Is64Bit(T.isAArch64()) {}

  bool is64Bit() const { return Is64Bit; }

  /// Register alias classes: 'r' (RN, 32-bit only), 's' (SN), 'd' (DN) and
  /// 'q' (QN).
  std::optional<char> aliasClassForDirective(StringRef Directive) const {
    if (Directive.equals_insensitive("SN"))
      return 's';
    if (Directive.equals_insensitive("DN"))
      return 'd';
    if (Directive.equals_insensitive("QN"))
      return 'q';
    if (!Is64Bit && Directive.equals_insensitive("RN"))
      return 'r';
    return std::nullopt;
  }

  unsigned maxRegisterNumber(char Class) const {
    switch (Class) {
    case 'r':
      return 15;
    case 'q':
      return Is64Bit ? 31 : 15;
    default:
      return 31;
    }
  }

  /// sN, dN, qN (and rN on 32-bit ARM).
  std::optional<std::pair<char, unsigned>>
  parseAliasableRegister(StringRef Name) const {
    if (Name.size() < 2)
      return std::nullopt;
    char Class = toLower(Name.front());
    if (Class != 's' && Class != 'd' && Class != 'q' &&
        (Is64Bit || Class != 'r'))
      return std::nullopt;
    unsigned Number;
    if (Name.drop_front().getAsInteger(10, Number) ||
        Number > maxRegisterNumber(Class))
      return std::nullopt;
    return std::make_pair(Class, Number);
  }

  std::optional<std::pair<char, unsigned>>
  parseCoreRegisterAlias(StringRef Name) const {
    if (Is64Bit)
      return std::nullopt;
    if (Name.equals_insensitive("sp"))
      return std::make_pair('r', 13u);
    if (Name.equals_insensitive("lr"))
      return std::make_pair('r', 14u);
    if (Name.equals_insensitive("pc"))
      return std::make_pair('r', 15u);
    return std::nullopt;
  }

  bool isPredefinedRegisterName(StringRef Name) const {
    if (parseAliasableRegister(Name) || parseCoreRegisterAlias(Name))
      return true;
    if (!Is64Bit)
      return Name.equals_insensitive("ip") || Name.equals_insensitive("fp") ||
             Name.equals_insensitive("sb") || Name.equals_insensitive("sl");
    if (Name.equals_insensitive("sp") || Name.equals_insensitive("wsp") ||
        Name.equals_insensitive("xzr") || Name.equals_insensitive("wzr") ||
        Name.equals_insensitive("fp") || Name.equals_insensitive("lr") ||
        Name.equals_insensitive("ip0") || Name.equals_insensitive("ip1"))
      return true;
    if (Name.size() < 2)
      return false;
    unsigned Number;
    if (Name.drop_front().getAsInteger(10, Number))
      return false;
    switch (toLower(Name.front())) {
    case 'x':
    case 'w':
    case 'b':
    case 'h':
    case 'v':
    case 'z':
      return Number <= 31;
    case 'p':
      return Number <= 15;
    default:
      return false;
    }
  }

  /// The :BASE: encoding of a storage-map base register.
  std::optional<unsigned> getStorageMapBaseEncoding(StringRef Register) const {
    Register = Register.trim();
    unsigned Number;
    if (!Is64Bit) {
      if (std::optional<std::pair<char, unsigned>> Alias =
              parseCoreRegisterAlias(Register))
        return Alias->second;
      if (!Register.consume_front_insensitive("r") ||
          Register.getAsInteger(10, Number) || Number > 15)
        return std::nullopt;
      return Number;
    }
    if (Register.equals_insensitive("fp"))
      return 63;
    if (Register.equals_insensitive("lr"))
      return 64;
    if (Register.equals_insensitive("sp"))
      return 65;
    if (!Register.consume_front_insensitive("x") ||
        Register.getAsInteger(10, Number) || Number > 30)
      return std::nullopt;
    return 34 + Number;
  }

  StringRef getRelocationName(uint64_t Type) const {
    if (!Is64Bit) {
      switch (Type) {
      case COFF::IMAGE_REL_ARM_ABSOLUTE:
        return "IMAGE_REL_ARM_ABSOLUTE";
      case COFF::IMAGE_REL_ARM_ADDR32:
        return "IMAGE_REL_ARM_ADDR32";
      case COFF::IMAGE_REL_ARM_ADDR32NB:
        return "IMAGE_REL_ARM_ADDR32NB";
      case COFF::IMAGE_REL_ARM_SECTION:
        return "IMAGE_REL_ARM_SECTION";
      case COFF::IMAGE_REL_ARM_SECREL:
        return "IMAGE_REL_ARM_SECREL";
      case COFF::IMAGE_REL_ARM_MOV32T:
        return "IMAGE_REL_ARM_MOV32T";
      case COFF::IMAGE_REL_ARM_BRANCH20T:
        return "IMAGE_REL_ARM_BRANCH20T";
      case COFF::IMAGE_REL_ARM_BRANCH24T:
        return "IMAGE_REL_ARM_BRANCH24T";
      case COFF::IMAGE_REL_ARM_BLX23T:
        return "IMAGE_REL_ARM_BLX23T";
      case COFF::IMAGE_REL_ARM_REL32:
        return "IMAGE_REL_ARM_REL32";
      default:
        return {};
      }
    }
    switch (Type) {
    case COFF::IMAGE_REL_ARM64_ABSOLUTE:
      return "IMAGE_REL_ARM64_ABSOLUTE";
    case COFF::IMAGE_REL_ARM64_ADDR32:
      return "IMAGE_REL_ARM64_ADDR32";
    case COFF::IMAGE_REL_ARM64_ADDR32NB:
      return "IMAGE_REL_ARM64_ADDR32NB";
    case COFF::IMAGE_REL_ARM64_BRANCH26:
      return "IMAGE_REL_ARM64_BRANCH26";
    case COFF::IMAGE_REL_ARM64_PAGEBASE_REL21:
      return "IMAGE_REL_ARM64_PAGEBASE_REL21";
    case COFF::IMAGE_REL_ARM64_REL21:
      return "IMAGE_REL_ARM64_REL21";
    case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12A:
      return "IMAGE_REL_ARM64_PAGEOFFSET_12A";
    case COFF::IMAGE_REL_ARM64_PAGEOFFSET_12L:
      return "IMAGE_REL_ARM64_PAGEOFFSET_12L";
    case COFF::IMAGE_REL_ARM64_SECREL:
      return "IMAGE_REL_ARM64_SECREL";
    case COFF::IMAGE_REL_ARM64_SECREL_LOW12A:
      return "IMAGE_REL_ARM64_SECREL_LOW12A";
    case COFF::IMAGE_REL_ARM64_SECREL_HIGH12A:
      return "IMAGE_REL_ARM64_SECREL_HIGH12A";
    case COFF::IMAGE_REL_ARM64_SECREL_LOW12L:
      return "IMAGE_REL_ARM64_SECREL_LOW12L";
    case COFF::IMAGE_REL_ARM64_TOKEN:
      return "IMAGE_REL_ARM64_TOKEN";
    case COFF::IMAGE_REL_ARM64_SECTION:
      return "IMAGE_REL_ARM64_SECTION";
    case COFF::IMAGE_REL_ARM64_ADDR64:
      return "IMAGE_REL_ARM64_ADDR64";
    case COFF::IMAGE_REL_ARM64_BRANCH19:
      return "IMAGE_REL_ARM64_BRANCH19";
    case COFF::IMAGE_REL_ARM64_BRANCH14:
      return "IMAGE_REL_ARM64_BRANCH14";
    case COFF::IMAGE_REL_ARM64_REL32:
      return "IMAGE_REL_ARM64_REL32";
    default:
      return {};
    }
  }

  /// The number of bytes a RELOC of type \p Type patches, for the A4205
  /// size check.
  unsigned getRelocationSize(uint64_t Type) const {
    if (Is64Bit)
      return Type == COFF::IMAGE_REL_ARM64_ADDR64    ? 8
             : Type == COFF::IMAGE_REL_ARM64_SECTION ? 2
             : Type == COFF::IMAGE_REL_ARM64_ABSOLUTE ? 0
                                                      : 4;
    return Type == COFF::IMAGE_REL_ARM_SECTION    ? 2
           : Type == COFF::IMAGE_REL_ARM_ABSOLUTE ? 0
                                                  : 4;
  }

  /// The CodeView CPU type recorded in S_COMPILE3.
  unsigned getCodeViewMachine() const {
    return Is64Bit ? static_cast<unsigned>(codeview::CPUType::ARM64)
                   : static_cast<unsigned>(codeview::CPUType::ARMNT);
  }

  /// The instruction used to pad CODEALIGN areas and its size.
  std::pair<uint64_t, unsigned> getCodeAlignmentFill() const {
    return Is64Bit ? std::make_pair(uint64_t(0xd503201f), 4u)
                   : std::make_pair(uint64_t(0xbf00), 2u);
  }

  /// The instruction size that an ALIGN fill value uses in code areas.
  unsigned getCodeFillSize() const { return Is64Bit ? 4 : 2; }

  /// The default AREA alignment, as a power of two.
  unsigned getDefaultAreaAlignment() const { return Is64Bit ? 3 : 2; }

  /// The Microsoft assembler build this matches: armasm64 14.52.36815 and
  /// armasm 14.44.35229 (the last release with 32-bit ARM tools). It is
  /// reported by {ARMASM_VERSION} and recorded in @comp.id.
  uint64_t getToolBuild() const { return Is64Bit ? 36815 : 35229; }
  uint64_t getARMASMVersion() const {
    return Is64Bit ? 1452'00000 + getToolBuild() : 1444'00000 + getToolBuild();
  }
  /// @comp.id: product identifier 0x0103 (MASM) and the build number.
  uint64_t getCompId() const { return (0x0103 << 16) | getToolBuild(); }

  /// Whether data directives in code areas pad to their alignment with NOP
  /// instructions (armasm64) or with zeros (armasm).
  bool padsDataWithCode() const { return Is64Bit; }
};

//===----------------------------------------------------------------------===//
// ARMASMParser
//===----------------------------------------------------------------------===//

class ARMASMParser final : public MCAsmParser, private ExpressionContext {
  struct SourceLine {
    /// The line without its terminator.
    StringRef Text;
    /// The whole buffer containing Text; null-terminated.
    StringRef Buffer;
    StringRef Filename;
    unsigned Line = 0;
    SMLoc getLoc() const { return SMLoc::getFromPointer(Text.data()); }
  };

  struct MacroDefinition {
    struct Parameter {
      std::string Name;
      std::optional<std::string> DefaultValue;
    };
    std::optional<std::string> LabelParameter;
    SmallVector<Parameter, 4> Parameters;
    SmallVector<SourceLine, 8> Body;
  };

  enum class InputKind { MainFile, IncludeFile, Macro };
  enum class ControlKind { If, While };

  struct ControlFrame {
    ControlKind Kind;
    bool ParentActive;
    bool Active;
    bool BranchTaken = false;
    bool SawElse = false;
    size_t WhileLine = 0;
    size_t BodyLine = 0;
  };

  enum class ExportType { None, Data, Function };

  struct RegisterAlias {
    char Class;
    unsigned Number;
  };

  struct StorageMapField {
    int64_t Offset = 0;
    std::optional<std::string> BaseRegister;
  };

  struct AreaDefinition {
    uint64_t Alignment = 1;
    bool UsesCodeAlignment = false;
    bool IsCode = false;
    bool IsNoInit = false;
    unsigned Characteristics = 0;
    std::string ComdatSymbol;
    COFF::COMDATType Selection = static_cast<COFF::COMDATType>(0);
    std::string AttributeSignature;
    MCSymbol *BaseSymbol = nullptr;
    MCSection *Section = nullptr;
    std::optional<unsigned> DebugCodeSection;
    bool IsPData = false;
  };

  struct DebugCodeSection {
    MCSection *Section;
    MCSymbol *StartSymbol;
    MCSymbol *EndSymbol;
    unsigned FunctionId;
    bool HasLines = false;
    unsigned LastFile = 0;
    unsigned LastLine = 0;
  };

  struct DebugSymbol {
    enum KindTy { Procedure, Label, Data };
    std::string Name;
    MCSymbol *Symbol;
    MCSymbol *EndSymbol;
    KindTy Kind;
    bool IsExternal;
    /// The CodeView type of a data symbol.
    unsigned Type = 0;
    /// A symbol at the start of the symbol's area: armasm gives the section
    /// index of labels and data relative to the section symbol.
    MCSymbol *SectionSymbol = nullptr;
    /// The lines of a procedure's PROC and ENDP; EndLine is 0 until ENDP.
    unsigned Line = 0;
    unsigned EndLine = 0;
  };

  /// The storage class and type armasm gives a symbol the source defines
  /// when it is not exported; an alias of the symbol gets them too.
  struct LocalSymbolClass {
    int StorageClass;
    std::optional<int> Type;
  };

  struct PendingWeakExternal {
    std::string Name;
    std::string Fallback;
    uint64_t Search = COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS;
    bool Conditional = false;
  };

  /// The most recent instruction or data definition, which a following RELOC
  /// directive applies to.
  struct PreviousDataDefinition {
    unsigned Size;
    bool IsSymbolic;
    bool IsInstruction;
  };

  /// A symbolic data value or branch whose emission is held back until the
  /// next statement, because a RELOC directive on the next line replaces its
  /// value with zero.
  struct PendingEmission {
    enum { None, Value, Branch } Kind = None;
    const MCExpr *Expr = nullptr;
    unsigned Size = 0;
    SMLoc Loc;
    SourceLine Line;
    std::string Text;
    size_t InstructionOffset = 0;
    std::string Mnemonic;
    /// The statement's {PC} label, for a branch emitted after it.
    MCSymbol *PCSymbol = nullptr;
  };

  /// Everything that is rebuilt on each pass.
  struct PassState {
    // Assembly control.
    VariableMap Variables;
    StringMap<uint64_t> Constants;
    StringMap<MacroDefinition> Macros;
    StringSet<> DefinedSymbols;
    VariableMap BuiltinVariables;
    unsigned ListingLine = 0;
    VariableMap *LocalVariables = nullptr;
    const StringMap<std::string> *MacroParameters = nullptr;
    /// The macro invocations enclosing the current line, outermost first.
    SmallVector<SourceLine, 4> MacroInvocations;
    /// The line being processed and its text after substitution.
    const char *ActiveLine = nullptr;
    std::string ActiveText;

    // Symbols and storage.
    StringMap<std::string> ConstantSymbols;
    StringMap<std::pair<SMLoc, std::pair<std::string, unsigned>>>
        ExportLocations;
    StringSet<> DefinedObjectSymbols;
    StringSet<> ExternalSymbols;
    StringSet<> CommonSymbols;
    StringMap<RegisterAlias> RegisterAliases;
    StorageMapField CurrentStorageMap;
    StringMap<StorageMapField> StorageMapFields;
    StringMap<VariableValue> RegisterRelativeValues;
    StringMap<uint64_t> SymbolSizes;
    StringMap<AreaDefinition> Areas;
    /// Areas in the order the source defines them.
    SmallVector<AreaDefinition *, 8> AreaOrder;
    SmallVector<PendingWeakExternal, 4> PendingWeakExternals;
    StringSet<> ReferencedNames;
    /// The order in which the source first mentions each symbol, which is
    /// the order names enter armasm's symbol hash table.
    StringMap<unsigned> SymbolMentions;
    StringMap<LocalSymbolClass> LocalSymbolClasses;

    // Current area and procedure.
    std::string CurrentAreaName;
    bool CurrentAreaIsCode = false;
    bool CurrentAreaIsNoInit = false;
    bool CurrentAreaIsPData = false;
    bool CurrentAreaUsesCodeAlignment = false;
    uint64_t CurrentAreaAlignment = 8;
    MCSymbol *CurrentAreaBaseSymbol = nullptr;
    std::optional<std::string> ActiveProcedureArea;
    bool AtProcedureStart = false;
    unsigned NumericLabelScope = 0;
    std::string NumericLabelRoutName;
    DenseSet<unsigned> DefinedNumericLabels;
    /// How many definitions of each numeric local label (by scope and
    /// number) this pass has seen.
    DenseMap<unsigned, unsigned> NumericLabelDefinitions;
    unsigned PCSymbolCount = 0;
    MCSymbol *CurrentPCSymbol = nullptr;

    // A #line directive: lines after PhysicalLine of PhysicalFile are
    // attributed to Filename, starting at Line.
    struct LineDirective {
      std::string PhysicalFile;
      unsigned PhysicalLine;
      std::string Filename;
      unsigned Line;
    };
    std::optional<LineDirective> ActiveLineDirective;
    /// Debug file numbers, assigned as files first get line entries.
    StringMap<unsigned> DebugFileNumbers;

    // Statement context.
    bool FirstWasDefined = false;
    /// Whether the current statement starts with a label in column 1.
    bool StartsWithLabel = false;
    std::string CurrentFilename;
    unsigned CurrentLine = 0;
    unsigned CurrentListingLine = 0;
    SMLoc CurrentLoc;
    std::optional<PreviousDataDefinition> PreviousData;
    PendingEmission Pending;

    // Debug information.
    SmallVector<DebugCodeSection, 4> DebugCodeSections;
    StringMap<unsigned> DebugCodeSectionIndices;
    std::optional<unsigned> CurrentDebugCodeSection;
    SmallVector<DebugSymbol, 16> DebugSymbols;
    StringSet<> DebugSymbolNames;
    std::optional<unsigned> ActiveDebugProcedure;
    bool SawDebugSymbol = false;
    // Every source file read, and files named by #line, for CodeView.
    StringSet<> DebugFileSet;
  };

  ARMASMParserOptions Opts;
  DialectTraits Traits;
  unsigned Pass = 1;
  PassState S;

  // Final definitions from pass 1, used for forward references in pass 2.
  StringMap<RegisterAlias> FinalRegisterAliases;
  StringMap<StorageMapField> FinalStorageMapFields;
  StringMap<uint64_t> CollectedSizes;
  StringMap<ExportType> CollectedExports;
  /// The _lc symbol names of each numeric local label's definitions (by scope
  /// and number), in source order, collected in pass 1 so that references in
  /// either direction can name the symbol armasm relocates against.
  DenseMap<unsigned, SmallVector<std::string, 1>> NumericLabelNames;
  SmallVector<std::string, 4> DebugFileOrder;


  /// Buffers loaded by GET/INCLUDE, reused by pass 2.
  StringMap<unsigned> IncludedBuffers;
  /// Buffers holding substituted lines, mapped to the source line they came
  /// from, for diagnostics.
  DenseMap<unsigned, SourceLine> SyntheticBuffers;

  /// The buffer the lexer is currently reading.
  StringRef CurrentLexerBuffer;

  StringMap<ExtensionDirectiveHandler> ExtensionDirectiveMap;

  bool emitting() const { return Pass == 2; }

  //===--------------------------------------------------------------------===//
  // Diagnostics
  //===--------------------------------------------------------------------===//

  void emitDiagnostic(const SMDiagnostic &Diagnostic) {
    if (SourceMgr::DiagHandlerTy Handler = SrcMgr.getDiagHandler())
      Handler(Diagnostic, SrcMgr.getDiagContext());
    else
      Diagnostic.print(nullptr, errs());
  }

  /// Notes naming the macro invocations that led to a diagnostic, innermost
  /// first.
  void reportMacroInvocations() {
    for (const SourceLine &Invocation : llvm::reverse(S.MacroInvocations))
      emitDiagnostic(SMDiagnostic(SrcMgr, Invocation.getLoc(),
                                  Invocation.Filename, Invocation.Line, -1,
                                  SourceMgr::DK_Note, "", Invocation.Text, {},
                                  {}));
  }

  void report(const SourceLine *Line, SMLoc Loc, SourceMgr::DiagKind Kind,
              const Twine &Message, int Column = -1) {
    if (Kind == SourceMgr::DK_Error)
      HadError = true;
    if (!Line) {
      SrcMgr.PrintMessage(Loc, Kind, Message);
      return;
    }
    // Show the line as substituted when it is the one being processed.
    StringRef Contents = Line->Text;
    if (Line->Text.data() && Line->Text.data() == S.ActiveLine)
      Contents = S.ActiveText;
    // Line 0 marks a diagnostic about a whole file.
    emitDiagnostic(SMDiagnostic(SrcMgr, Loc, Line->Filename,
                                Line->Line ? static_cast<int>(Line->Line) : -1,
                                Column, Kind, Message.str(), Contents, {},
                                {}));
    if (Line->Line)
      reportMacroInvocations();
  }

  /// Report a source error that stops assembly, as ARMASM does for most
  /// source errors. Always returns true.
  bool fatal(const SourceLine &Line, const Twine &Message) {
    report(&Line, Line.getLoc(), SourceMgr::DK_Error, Message);
    return true;
  }

  bool fatalFile(StringRef Filename, unsigned LineNumber,
                 const Twine &Message) {
    SourceLine Line;
    Line.Filename = Filename;
    Line.Line = LineNumber;
    return fatal(Line, Message);
  }

  bool warningEnabled(unsigned Code) const {
    return !Opts.NoWarnings && !Opts.IgnoredWarnings.contains(Code);
  }

  void warn(const SourceLine &Line, unsigned Code, const Twine &Message) {
    if (!warningEnabled(Code))
      return;
    report(&Line, Line.getLoc(), SourceMgr::DK_Warning,
           "A" + Twine(Code) + ": " + Message);
  }

  /// Diagnostics belonging to assembly control are reported by pass 1;
  /// those belonging to statements by pass 2.
  void controlWarning(const SourceLine &Line, unsigned Code,
                      const Twine &Message) {
    if (Pass == 1)
      warn(Line, Code, Message);
  }

  void statementWarning(const SourceLine &Line, unsigned Code,
                        const Twine &Message) {
    if (emitting())
      warn(Line, Code, Message);
  }

  /// A statement error. Pass 1 abandons the statement silently; pass 2
  /// reports it. Always returns true.
  bool statementError(const SourceLine &Line, const Twine &Message) {
    if (!emitting())
      return true;
    return fatal(Line, Message);
  }

  bool symbolConflict(const SourceLine &Line, StringRef Name) {
    return statementError(Line, "A2026: multiple symbol definition or "
                                "incompatibility: " +
                                    Name);
  }

  //===--------------------------------------------------------------------===//
  // ExpressionContext
  //===--------------------------------------------------------------------===//

  std::optional<uint64_t> lookUpConstant(StringRef Name) const override {
    auto It = S.Constants.find(Name);
    if (It == S.Constants.end())
      return std::nullopt;
    return It->second;
  }

  std::optional<VariableValue>
  lookUpRegisterRelative(StringRef Name) const override {
    auto It = S.RegisterRelativeValues.find(Name);
    if (It != S.RegisterRelativeValues.end())
      return It->second;
    // Unbased storage-map fields evaluate to their offset.
    const StringMap<StorageMapField> &Fields =
        emitting() ? FinalStorageMapFields : S.StorageMapFields;
    auto Field = Fields.find(Name);
    if (Field != Fields.end() && !Field->second.BaseRegister)
      return VariableValue::arithmetic(Field->second.Offset);
    return std::nullopt;
  }

  bool lookUpRegisterConstant(StringRef Name,
                              std::optional<unsigned> &Number) const override {
    // armasm (32-bit) numbers every register class and its aliases: rN, sN,
    // dN, qN, sp/lr/pc, ip/fp/sb/sl and RN/SN/DN/QN names.
    if (Traits.is64Bit())
      return false;
    if (std::optional<std::pair<char, unsigned>> Register =
            Traits.parseAliasableRegister(Name))
      Number = Register->second;
    else if (std::optional<std::pair<char, unsigned>> Core =
                 Traits.parseCoreRegisterAlias(Name))
      Number = Core->second;
    else if (Name.equals_insensitive("ip"))
      Number = 12;
    else if (Name.equals_insensitive("fp"))
      Number = 11;
    else if (Name.equals_insensitive("sl"))
      Number = 10;
    else if (Name.equals_insensitive("sb"))
      Number = 9;
    else if (auto Alias = S.RegisterAliases.find(Name);
             Alias != S.RegisterAliases.end())
      Number = Alias->second.Number;
    return true;
  }

  std::optional<uint64_t> lookUpSymbolSize(StringRef Name) const override {
    auto It = S.SymbolSizes.find(Name);
    if (It == S.SymbolSizes.end())
      return std::nullopt;
    return It->second;
  }

  bool isSymbolDefined(StringRef Name) const override {
    return S.DefinedSymbols.contains(Name);
  }

  const VariableMap *getBuiltins() const override {
    return &S.BuiltinVariables;
  }

  MCContext *getContext() override { return &Ctx; }

  const MCExpr *getSymbolExpr(StringRef Name) override {
    if (!emitting())
      return nullptr;
    S.ReferencedNames.insert(Name);
    auto Constant = S.ConstantSymbols.find(Name);
    MCSymbol *Symbol =
        getSymbol(Constant == S.ConstantSymbols.end() ? Name : Constant->second);
    // The statement's location lets fixup errors at layout name the line.
    return MCSymbolRefExpr::create(Symbol, Ctx, S.CurrentLoc);
  }

  const MCExpr *getLocationExpr(bool IsPC) override {
    if (!emitting())
      return nullptr;
    if (IsPC && S.CurrentPCSymbol)
      return MCSymbolRefExpr::create(S.CurrentPCSymbol, Ctx, S.CurrentLoc);
    MCSymbol *Here = Ctx.createTempSymbol();
    Out.emitLabel(Here);
    return MCSymbolRefExpr::create(Here, Ctx, S.CurrentLoc);
  }

  const MCExpr *getNumericLabelExpr(char Direction, unsigned Number,
                                    StringRef RoutName,
                                    std::string &Error) override {
    if (!RoutName.empty() && RoutName != S.NumericLabelRoutName) {
      Error = "numeric local label ROUT name mismatch";
      return nullptr;
    }
    if (!Direction)
      Direction = S.DefinedNumericLabels.contains(Number) ? 'b' : 'f';
    // Name the definition's _lc symbol, as armasm does in relocations.
    unsigned Key = S.NumericLabelScope * 100 + Number;
    unsigned Seen = S.NumericLabelDefinitions.lookup(Key);
    auto Names = NumericLabelNames.find(Key);
    if (Names != NumericLabelNames.end() && (Direction == 'f' || Seen)) {
      unsigned Index = Direction == 'b' ? Seen - 1 : Seen;
      if (Index < Names->second.size())
        return MCSymbolRefExpr::create(
            Ctx.getOrCreateSymbol(Names->second[Index]), Ctx, S.CurrentLoc);
    }
    MCSymbol *Symbol =
        Ctx.getDirectionalLocalSymbol(Key, Direction == 'b');
    return MCSymbolRefExpr::create(Symbol, Ctx, S.CurrentLoc);
  }

  VariableLookup effectiveVariables() const {
    return VariableLookup(S.Variables, S.LocalVariables);
  }

  Expected<VariableValue> evaluate(StringRef Expression,
                                   bool AllowSymbolic = false) {
    ExpressionParser Parser(Expression, effectiveVariables(), *this,
                            Opts.NoEscape, AllowSymbolic && emitting());
    return Parser.parse();
  }

  bool evaluateAbsolute(const SourceLine &Line, StringRef Expression,
                        uint64_t &Result) {
    Expected<VariableValue> Value = evaluate(Expression);
    if (!Value)
      return statementError(Line, toString(Value.takeError()));
    if (Value->Kind != VariableKind::Arithmetic)
      return statementError(Line, "expected absolute numeric expression");
    Result = Value->Arithmetic;
    return false;
  }

  //===--------------------------------------------------------------------===//
  // Variables
  //===--------------------------------------------------------------------===//

  static void declareVariable(StringRef Name, VariableKind Kind,
                              VariableMap &Variables) {
    VariableValue Initial =
        Kind == VariableKind::Arithmetic ? VariableValue::arithmetic(0)
        : Kind == VariableKind::Logical  ? VariableValue::logical(false)
                                         : VariableValue::string("");
    Variables[Name] = std::move(Initial);
  }

  llvm::Error assignVariable(StringRef Name, StringRef Directive,
                       StringRef Expression, bool ImplicitDeclaration,
                       VariableMap &Target) {
    VariableKind Kind =
        Directive.equals_insensitive("SETA")   ? VariableKind::Arithmetic
        : Directive.equals_insensitive("SETL") ? VariableKind::Logical
                                               : VariableKind::String;
    auto It = Target.find(Name);
    if (It == Target.end()) {
      if (!ImplicitDeclaration)
        return createStringError(inconvertibleErrorCode(),
                                 "assignment to undeclared variable '" + Name +
                                     "'");
      declareVariable(Name, Kind, Target);
      It = Target.find(Name);
    }
    if (It->second.Kind != Kind)
      return createStringError(
          inconvertibleErrorCode(),
          "cannot assign " + variableKindName(Kind) + " value to " +
              variableKindName(It->second.Kind) + " variable '" + Name + "'");

    Expected<VariableValue> Value = evaluate(Expression);
    if (!Value)
      return Value.takeError();
    if (Value->Kind != Kind)
      return createStringError(inconvertibleErrorCode(),
                               Directive + " requires a " +
                                   variableKindName(Kind) + " expression");
    // The evaluation may have inserted into the map; look the name up again.
    Target[Name] = std::move(*Value);
    return llvm::Error::success();
  }

  llvm::Error executePredefine(StringRef Predefine) {
    std::string Substituted = substituteVariables(Predefine, S.Variables);
    StringRef Tail = stripComment(Substituted).trim();
    StringRef NameToken = takeToken(Tail);
    StringRef Directive = takeToken(Tail);
    if (!isValidVariableName(NameToken) || !isSetDirective(Directive) ||
        Tail.empty())
      return createStringError(
          inconvertibleErrorCode(),
          "expected '<variable> SETA|SETL|SETS <expression>'");
    return assignVariable(unquoteIdentifier(NameToken), Directive, Tail,
                          /*ImplicitDeclaration=*/true, S.Variables);
  }

  //===--------------------------------------------------------------------===//
  // Assembly control (the line engine)
  //===--------------------------------------------------------------------===//

  std::string prepareLine(const SourceLine &Line) const {
    if (S.MacroParameters)
      return substituteVariables(
          substituteMacroParameters(Line.Text, *S.MacroParameters),
          effectiveVariables());
    return substituteVariables(Line.Text, effectiveVariables());
  }

  bool evaluateCondition(const SourceLine &Line, StringRef Expression,
                         bool &Result) {
    Expected<VariableValue> Value = evaluate(Expression);
    if (!Value)
      return fatal(Line, toString(Value.takeError()));
    if (Value->Kind == VariableKind::Logical) {
      Result = Value->Logical;
      return false;
    }
    if (Value->Kind == VariableKind::Arithmetic) {
      Result = Value->Arithmetic != 0;
      return false;
    }
    return fatal(Line, "condition requires a numeric or logical expression");
  }

  static void splitLines(StringRef Buffer, StringRef Filename,
                         SmallVectorImpl<SourceLine> &Lines) {
    StringRef Remaining = Buffer;
    unsigned LineNumber = 0;
    while (!Remaining.empty()) {
      auto [Line, Rest] = Remaining.split('\n');
      Remaining = Rest;
      Line.consume_back("\r");
      SourceLine Source;
      Source.Text = Line;
      Source.Buffer = Buffer;
      Source.Filename = Filename;
      Source.Line = ++LineNumber;
      Lines.push_back(Source);
    }
  }

  std::optional<unsigned> openIncludeFile(StringRef Filename,
                                          StringRef IncludingFile,
                                          std::error_code &EC) {
    SmallVector<SmallString<256>, 4> Candidates;
    if (sys::path::is_absolute(Filename)) {
      Candidates.emplace_back(Filename);
    } else {
      SmallString<256> Path(sys::path::parent_path(IncludingFile));
      sys::path::append(Path, Filename);
      Candidates.push_back(Path);
      Candidates.emplace_back(Filename);
      for (StringRef IncludeDir : Opts.IncludeDirs) {
        Path = IncludeDir;
        sys::path::append(Path, Filename);
        Candidates.push_back(Path);
      }
    }

    EC = std::make_error_code(std::errc::no_such_file_or_directory);
    for (const SmallString<256> &Candidate : Candidates) {
      auto Existing = IncludedBuffers.find(Candidate);
      if (Existing != IncludedBuffers.end())
        return Existing->second;
      ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
          MemoryBuffer::getFile(Candidate, /*IsText=*/true);
      if (Buffer) {
        unsigned ID = SrcMgr.AddNewSourceBuffer(std::move(*Buffer), SMLoc());
        IncludedBuffers[Candidate] = ID;
        return ID;
      }
      EC = Buffer.getError();
    }
    return std::nullopt;
  }

  bool defineMacroFromSource(ArrayRef<SourceLine> Lines, size_t &LineIndex, bool Active,
                   unsigned NestingDepth) {
    const SourceLine &MacroLine = Lines[LineIndex];
    if (LineIndex + 1 == Lines.size())
      return fatal(MacroLine, "missing macro prototype and MEND directive");

    size_t PrototypeIndex = LineIndex + 1;
    size_t EndIndex = PrototypeIndex + 1;
    unsigned DefinitionDepth = 1;
    for (; EndIndex != Lines.size(); ++EndIndex) {
      std::string Expanded = prepareLine(Lines[EndIndex]);
      StringRef Tail = stripComment(Expanded).trim();
      StringRef First = takeToken(Tail);
      if (First.equals_insensitive("MACRO")) {
        if (NestingDepth + DefinitionDepth == 256)
          return fatal(Lines[EndIndex], "assembly control nesting limit exceeded");
        ++DefinitionDepth;
      } else if (First.equals_insensitive("MEND") && --DefinitionDepth == 0) {
        break;
      }
    }
    if (EndIndex == Lines.size())
      return fatalFile(Lines.back().Filename, Lines.back().Line + 1,
                "unexpected end of file; missing MEND directive");

    LineIndex = EndIndex + 1;
    if (!Active)
      return false;

    const SourceLine &Prototype = Lines[PrototypeIndex];
    std::string ExpandedPrototype = prepareLine(Prototype);
    StringRef Tail = stripComment(ExpandedPrototype).trim();
    StringRef NameToken = takeToken(Tail);
    std::optional<std::string> LabelParameter;
    if (NameToken.consume_front("$")) {
      if (!isValidVariableName(NameToken))
        return fatal(Prototype, "invalid macro label parameter");
      LabelParameter = NameToken.str();
      NameToken = takeToken(Tail);
    }
    if (!isValidVariableName(NameToken))
      return fatal(Prototype, "invalid macro name");

    MacroDefinition Definition;
    Definition.LabelParameter = std::move(LabelParameter);
    StringSet<> ParameterNames;
    if (Definition.LabelParameter)
      ParameterNames.insert(*Definition.LabelParameter);
    if (!Tail.empty()) {
      SmallVector<StringRef, 8> Parameters;
      splitOperands(Tail, Parameters);
      for (StringRef Parameter : Parameters) {
        Parameter = Parameter.trim();
        if (!Parameter.consume_front("$"))
          return fatal(Prototype, "macro parameters must begin with '$'");
        size_t Equal = Parameter.find('=');
        StringRef Name = Parameter.take_front(Equal).trim();
        if (!isValidVariableName(Name))
          return fatal(Prototype, "invalid macro parameter name");
        if (!ParameterNames.insert(Name).second)
          return fatal(Prototype, "duplicate macro parameter '" + Name + "'");
        std::optional<std::string> DefaultValue;
        if (Equal != StringRef::npos)
          DefaultValue =
              normalizeMacroArgument(Parameter.drop_front(Equal + 1));
        Definition.Parameters.push_back({Name.str(), std::move(DefaultValue)});
      }
    }
    Definition.Body.append(Lines.begin() + PrototypeIndex + 1,
                           Lines.begin() + EndIndex);

    if (!S.Macros.try_emplace(NameToken, std::move(Definition)).second)
      return fatal(Lines[EndIndex],
                   "macro name '" + NameToken + "' is already defined");
    return false;
  }

  bool expandMacro(const MacroDefinition &Definition, StringRef Name,
                   bool HasLabel, StringRef Label, StringRef ArgumentText,
                   const SourceLine &Invocation, unsigned NestingDepth) {
    SmallVector<StringRef, 8> Arguments;
    if (!ArgumentText.trim().empty())
      splitOperands(ArgumentText, Arguments);
    if (Arguments.size() > Definition.Parameters.size())
      return fatal(Invocation, "too many arguments to macro '" + Name + "'");

    StringMap<std::string> Bindings;
    for (auto [Index, Parameter] : llvm::enumerate(Definition.Parameters)) {
      StringRef Argument =
          Index < Arguments.size() ? Arguments[Index].trim() : StringRef();
      if (Argument == "|" && Parameter.DefaultValue)
        Bindings[Parameter.Name] = *Parameter.DefaultValue;
      else
        Bindings[Parameter.Name] =
            Argument == "|" ? std::string() : normalizeMacroArgument(Argument);
    }
    if (Definition.LabelParameter)
      Bindings[*Definition.LabelParameter] = HasLabel ? Label.str() : "";
    else if (HasLabel && processStatement(Invocation, Label))
      return true;

    VariableMap Locals;
    VariableMap *SavedLocals = S.LocalVariables;
    const StringMap<std::string> *SavedParameters = S.MacroParameters;
    S.LocalVariables = &Locals;
    S.MacroParameters = &Bindings;
    S.MacroInvocations.push_back(Invocation);
    bool DidExit = false;
    scope_exit Restore([&] {
      S.MacroParameters = SavedParameters;
      S.LocalVariables = SavedLocals;
      S.MacroInvocations.pop_back();
    });
    return processLines(Definition.Body, InputKind::Macro, NestingDepth,
                        DidExit, Invocation.Filename);
  }

  bool assignCurrentVariable(const SourceLine &Line, StringRef Name,
                             StringRef Directive, StringRef Expression) {
    bool IsLocal = S.LocalVariables && S.LocalVariables->contains(Name);
    VariableMap &Target = IsLocal ? *S.LocalVariables : S.Variables;
    if (llvm::Error Err = assignVariable(Name, Directive, Expression,
                                   /*ImplicitDeclaration=*/false, Target))
      return fatal(Line, toString(std::move(Err)));
    return false;
  }

  bool processBuffer(unsigned BufferID, InputKind Kind, unsigned NestingDepth,
                     bool *ExitedMacro = nullptr) {
    const MemoryBuffer *Buffer = SrcMgr.getMemoryBuffer(BufferID);
    StringRef Filename = Buffer->getBufferIdentifier();
    S.DebugFileSet.insert(
        normalizeDebugPath(sys::path::convert_to_slash(Filename)));
    SmallVector<SourceLine, 0> Lines;
    splitLines(Buffer->getBuffer(), Filename, Lines);
    bool MacroExit = false;
    if (processLines(Lines, Kind, NestingDepth, MacroExit, Filename))
      return true;
    if (ExitedMacro)
      *ExitedMacro = MacroExit;
    return false;
  }

  bool processLines(ArrayRef<SourceLine> Lines, InputKind Kind,
                    unsigned NestingDepth, bool &MacroExit,
                    StringRef InputFilename) {
    SmallVector<ControlFrame, 8> Controls;
    bool SawEnd = false;
    size_t LineIndex = 0;
    while (LineIndex != Lines.size()) {
      const SourceLine &Line = Lines[LineIndex];
      S.ListingLine = Kind == InputKind::Macro
                          ? S.ListingLine + 1
                          : std::max(S.ListingLine + 1, Line.Line);
      S.BuiltinVariables["INPUTFILE"] =
          VariableValue::string(sys::path::convert_to_slash(Line.Filename));
      S.BuiltinVariables["LINENUM"] = VariableValue::arithmetic(Line.Line);
      bool Active = Controls.empty() || Controls.back().Active;
      std::string SubstitutedLine = prepareLine(Line);
      S.ActiveLine = Line.Text.data();
      S.ActiveText = SubstitutedLine;
      StringRef Statement = stripComment(SubstitutedLine).trim();
      StringRef Tail = Statement;
      StringRef First = takeToken(Tail);
      StringRef AfterFirst = Tail;
      StringRef Second = takeToken(AfterFirst);

      if (First.equals_insensitive("MACRO")) {
        if (!Tail.empty())
          return fatal(Line, "unexpected tokens after MACRO directive");
        if (defineMacroFromSource(Lines, LineIndex, Active,
                        NestingDepth + static_cast<unsigned>(Controls.size())))
          return true;
        continue;
      }

      if (First.equals_insensitive("IF") || First == "[") {
        if (NestingDepth + Controls.size() == 256)
          return fatal(Line, "assembly control nesting limit exceeded");
        bool Condition = false;
        if (Active && evaluateCondition(Line, Tail, Condition))
          return true;
        Controls.push_back({ControlKind::If, Active, Active && Condition,
                            Active && Condition});
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("ELIF")) {
        if (Controls.empty() || Controls.back().Kind != ControlKind::If)
          return fatal(Line, "ELIF directive without a matching IF");
        ControlFrame &Frame = Controls.back();
        if (Frame.SawElse)
          return fatal(Line, "ELIF directive after ELSE");
        bool Condition = false;
        if (Frame.ParentActive && evaluateCondition(Line, Tail, Condition))
          return true;
        bool WasTaken = Frame.BranchTaken;
        Frame.Active = Frame.ParentActive && !WasTaken && Condition;
        Frame.BranchTaken |= Frame.ParentActive && Condition;
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("ELSE") || First == "|") {
        if (Controls.empty() || Controls.back().Kind != ControlKind::If)
          return fatal(Line, "ELSE directive without a matching IF");
        if (!Tail.empty())
          return fatal(Line, "unexpected tokens after ELSE directive");
        ControlFrame &Frame = Controls.back();
        if (Frame.SawElse)
          return fatal(Line, "multiple ELSE directives in one IF structure");
        Frame.Active = Frame.ParentActive && !Frame.BranchTaken;
        Frame.BranchTaken = Frame.ParentActive;
        Frame.SawElse = true;
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("ENDIF") || First == "]") {
        if (Controls.empty() || Controls.back().Kind != ControlKind::If)
          return fatal(Line, "ENDIF directive without a matching IF");
        if (!Tail.empty())
          return fatal(Line, "unexpected tokens after ENDIF directive");
        Controls.pop_back();
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("WHILE")) {
        if (NestingDepth + Controls.size() == 256)
          return fatal(Line, "assembly control nesting limit exceeded");
        bool Condition = false;
        if (Active && evaluateCondition(Line, Tail, Condition))
          return true;
        ControlFrame Frame{ControlKind::While, Active, Active && Condition};
        Frame.WhileLine = LineIndex;
        Frame.BodyLine = LineIndex + 1;
        Controls.push_back(Frame);
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("WEND")) {
        if (Controls.empty() || Controls.back().Kind != ControlKind::While)
          return fatal(Line, "WEND directive without a matching WHILE");
        if (!Tail.empty())
          return fatal(Line, "unexpected tokens after WEND directive");
        ControlFrame &Frame = Controls.back();
        if (Frame.Active) {
          const SourceLine &WhileLine = Lines[Frame.WhileLine];
          std::string WhileText = prepareLine(WhileLine);
          StringRef Condition = stripComment(WhileText).trim();
          takeToken(Condition);
          bool Repeat;
          if (evaluateCondition(WhileLine, Condition, Repeat))
            return true;
          if (Repeat) {
            LineIndex = Frame.BodyLine;
            continue;
          }
        }
        Controls.pop_back();
        ++LineIndex;
        continue;
      }

      if (!Active) {
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("ENTRY")) {
        controlWarning(Line, 4038, "unimplemented directive entry");
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("ASSERT")) {
        SmallVector<StringRef, 2> Operands;
        splitOperands(Tail, Operands);
        if (Operands.front().empty())
          return fatal(Line, "expected assertion expression");
        Expected<VariableValue> Value = evaluate(Operands.front());
        if (!Value)
          return fatal(Line, toString(Value.takeError()));
        if (Value->Kind != VariableKind::Logical)
          return fatal(Line, "A2067: illegal expression type; expected bool");
        if (!Value->Logical)
          return fatal(Line, "A2170: assertion failed");
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("INFO") || First == "!") {
        SmallVector<StringRef, 3> Operands;
        splitOperands(Tail, Operands);
        if (Operands.size() != 2 || Operands[0].empty() || Operands[1].empty())
          return fatal(Line, "A2003: improper line syntax");
        Expected<VariableValue> Condition = evaluate(Operands[0]);
        if (!Condition)
          return fatal(Line, toString(Condition.takeError()));
        if (Condition->Kind != VariableKind::Arithmetic)
          return fatal(Line,
                "A2061: illegal expression type; expected absolute numeric");
        Expected<VariableValue> Message = evaluate(Operands[1]);
        if (!Message)
          return fatal(Line, toString(Message.takeError()));
        if (Message->Kind != VariableKind::String)
          return fatal(Line, "A2068: illegal expression type; expected string");
        if (Condition->Arithmetic)
          return fatal(Line, "A2170: assertion failed");
        controlWarning(Line, 4058, Message->String);
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("MEXIT")) {
        if (!S.MacroParameters) {
          controlWarning(
              Line, 4094,
              "improper program syntax; unexpected MEND/MEXIT directive");
          ++LineIndex;
          continue;
        }
        if (!Tail.empty())
          return fatal(Line, "unexpected tokens after MEXIT directive");
        MacroExit = true;
        return false;
      }

      if (First.equals_insensitive("MEND")) {
        controlWarning(
            Line, 4094,
            "improper program syntax; unexpected MEND/MEXIT directive");
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("END")) {
        if (!Controls.empty())
          return fatal(Line, Controls.back().Kind == ControlKind::If
                          ? "unexpected END before ENDIF directive"
                          : "unexpected END before WEND directive");
        if (Kind == InputKind::Macro)
          return fatal(Line, "END directive inside a macro expansion");
        if (Kind == InputKind::MainFile &&
            processStatement(Line, SubstitutedLine))
          return true;
        SawEnd = true;
        break;
      }

      if (First.equals_insensitive("INCLUDE") ||
          First.equals_insensitive("GET")) {
        StringRef IncludedFilename = takeToken(Tail);
        if (IncludedFilename.empty() || !Tail.empty() ||
            IncludedFilename.front() == '"' || IncludedFilename.front() == '\'')
          return fatal(Line, "expected include file name");
        if (NestingDepth + Controls.size() == 256)
          return fatal(Line, "assembly control nesting limit exceeded");
        std::error_code EC;
        std::optional<unsigned> Included =
            openIncludeFile(IncludedFilename, Line.Filename, EC);
        if (!Included)
          return fatal(Line, "unable to open include file '" + IncludedFilename +
                          "': " + EC.message());
        bool IncludedMacroExit = false;
        if (processBuffer(*Included, InputKind::IncludeFile,
                          NestingDepth + Controls.size() + 1,
                          &IncludedMacroExit))
          return true;
        if (IncludedMacroExit) {
          MacroExit = true;
          return false;
        }
        ++LineIndex;
        continue;
      }

      auto Macro = S.Macros.find(First);
      bool HasInvocationLabel = false;
      StringRef InvocationLabel;
      StringRef ArgumentText = Tail;
      bool MayHaveInvocationLabel =
          !SubstitutedLine.empty() && !isSpace(SubstitutedLine.front());
      if (Macro == S.Macros.end() && MayHaveInvocationLabel &&
          !Second.empty()) {
        Macro = S.Macros.find(Second);
        if (Macro != S.Macros.end()) {
          HasInvocationLabel = true;
          InvocationLabel = First;
          ArgumentText = AfterFirst;
        }
      }
      if (Macro != S.Macros.end()) {
        if (NestingDepth + Controls.size() == 256)
          return fatal(Line, "assembly control nesting limit exceeded");
        std::string MacroName = Macro->getKey().str();
        MacroDefinition Definition = Macro->second;
        if (expandMacro(Definition, MacroName, HasInvocationLabel,
                        InvocationLabel, ArgumentText, Line,
                        NestingDepth + Controls.size() + 1))
          return true;
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("AREA")) {
        SmallVector<StringRef, 8> Attributes;
        splitOperands(Tail, Attributes);
        if (!Attributes.empty()) {
          Expected<AreaName> Area = parseAreaName(Attributes.front());
          if (Area)
            S.BuiltinVariables["AREANAME"] =
                VariableValue::string(Area->Name.str());
          else
            consumeError(Area.takeError());
        }
      }

      S.FirstWasDefined =
          S.DefinedSymbols.contains(unquoteIdentifier(First));
      recordDefinedSymbols(SubstitutedLine, First, Second, Tail);

      std::optional<VariableKind> DeclarationKind;
      bool IsLocalDeclaration = false;
      if (First.equals_insensitive("GBLA"))
        DeclarationKind = VariableKind::Arithmetic;
      else if (First.equals_insensitive("GBLL"))
        DeclarationKind = VariableKind::Logical;
      else if (First.equals_insensitive("GBLS"))
        DeclarationKind = VariableKind::String;
      else if (First.equals_insensitive("LCLA")) {
        DeclarationKind = VariableKind::Arithmetic;
        IsLocalDeclaration = true;
      } else if (First.equals_insensitive("LCLL")) {
        DeclarationKind = VariableKind::Logical;
        IsLocalDeclaration = true;
      } else if (First.equals_insensitive("LCLS")) {
        DeclarationKind = VariableKind::String;
        IsLocalDeclaration = true;
      }
      if (DeclarationKind) {
        StringRef NameToken = takeToken(Tail);
        if (!isValidVariableName(NameToken) || !Tail.empty())
          return fatal(Line, "expected one variable name after " + First);
        StringRef Name = unquoteIdentifier(NameToken);
        if (S.Constants.contains(Name))
          return fatal(Line, "variable name '" + Name + "' is already defined");
        if (IsLocalDeclaration) {
          if (!S.LocalVariables || !S.MacroParameters)
            return fatal(Line, "local variables can only be declared within a macro");
          if (S.MacroParameters->contains(Name))
            return fatal(Line, "local variable '" + Name +
                            "' conflicts with a macro parameter");
          declareVariable(Name, *DeclarationKind, *S.LocalVariables);
          ++LineIndex;
          continue;
        }
        if (hasInternalSymbol(Name) && emitting())
          return symbolConflict(Line, Name);
        declareVariable(Name, *DeclarationKind, S.Variables);
      } else if (isSetDirective(Second)) {
        StringRef Name = unquoteIdentifier(First);
        if (!isValidVariableName(First) || AfterFirst.empty())
          return fatal(Line, "expected variable assignment expression");
        bool IsLocalAssignment =
            S.LocalVariables && S.LocalVariables->contains(Name);
        if (assignCurrentVariable(Line, Name, Second, AfterFirst))
          return true;
        if (IsLocalAssignment) {
          ++LineIndex;
          continue;
        }
      }

      if (processStatement(Line, SubstitutedLine))
        return true;
      ++LineIndex;
    }

    if (!Controls.empty()) {
      unsigned EndLine = Lines.empty() ? 1 : Lines.back().Line + 1;
      return fatalFile(InputFilename, EndLine,
                Controls.back().Kind == ControlKind::If
                    ? "unexpected end of file; missing ENDIF directive"
                    : "unexpected end of file; missing WEND directive");
    }
    if (!SawEnd && Kind == InputKind::IncludeFile)
      return fatalFile(InputFilename, 0,
                       "unexpected end of file; missing END directive");
    if (!SawEnd && Kind == InputKind::MainFile && Pass == 1 &&
        warningEnabled(4045)) {
      SourceLine FileOnly;
      FileOnly.Filename = InputFilename;
      report(&FileOnly, SMLoc(), SourceMgr::DK_Warning,
             "A4045: missing END directive");
    }
    return false;
  }

  void recordDefinedSymbols(StringRef Line, StringRef First, StringRef Second,
                            StringRef Tail) {
    if (First.equals_insensitive("IMPORT") ||
        First.equals_insensitive("EXTERN") ||
        First.equals_insensitive("COMMON")) {
      StringRef Symbol = takeToken(Tail).split(',').first;
      if (!Symbol.empty())
        S.DefinedSymbols.insert(unquoteIdentifier(Symbol));
    }
    if (First.equals_insensitive("ALIAS")) {
      SmallVector<StringRef, 2> Operands;
      splitOperands(Tail, Operands);
      if (Operands.size() == 2)
        S.DefinedSymbols.insert(unquoteIdentifier(Operands[1]));
    }
    if (!Line.empty() && !isSpace(Line.front()) &&
        (Second.empty() || isEquDirective(Second) ||
         Second.equals_insensitive("PROC") ||
         Second.equals_insensitive("FUNCTION") ||
         Second.equals_insensitive("FIELD") ||
         Second.equals_insensitive("ROUT") || isDataDirective(Second) ||
         isStorageDirective(Second)))
      S.DefinedSymbols.insert(unquoteIdentifier(First));
  }

  bool hasInternalSymbol(StringRef Name) const {
    return S.RegisterAliases.contains(Name) ||
           S.StorageMapFields.contains(Name);
  }

  bool conflictsWithObjectDefinition(StringRef Name) const {
    return hasInternalSymbol(Name) || S.ExternalSymbols.contains(Name) ||
           S.CommonSymbols.contains(Name);
  }

  // Statement processing, defined below.
  bool processStatement(const SourceLine &Line, StringRef Text);
  void collectStatement(const SourceLine &Line, StringRef Text);
  bool emitStatement(const SourceLine &Line, StringRef Text);

  //===--------------------------------------------------------------------===//
  // Emission helpers
  //===--------------------------------------------------------------------===//

  MCSymbol *getSymbol(StringRef Name) {
    S.SymbolMentions.try_emplace(Name, S.SymbolMentions.size());
    return Ctx.getOrCreateSymbol(Name);
  }

  bool isExported(StringRef Name) const {
    return CollectedExports.contains(Name);
  }

  void defineCOFFSymbol(MCSymbol *Symbol, int StorageClass,
                        std::optional<int> Type = std::nullopt) {
    Out.beginCOFFSymbolDef(Symbol);
    Out.emitCOFFSymbolStorageClass(StorageClass);
    if (Type)
      Out.emitCOFFSymbolType(*Type);
    Out.endCOFFSymbolDef();
  }

  /// Define a symbol the source defines with the storage class (and type)
  /// armasm gives it when it is not exported.
  void defineLocalSymbol(StringRef Name, MCSymbol *Symbol, int StorageClass,
                         std::optional<int> Type = std::nullopt) {
    S.LocalSymbolClasses[Name] = {StorageClass, Type};
    if (!isExported(Name))
      defineCOFFSymbol(Symbol, StorageClass, Type);
  }

  void registerDebugFunction(const DebugSymbol &Procedure);

  void emitLabel(MCSymbol *Symbol, SMLoc Loc) {
    Out.emitLabel(Symbol, Loc);
    getTargetParser().onLabelParsed(Symbol);
  }

  void flushPending(bool ReplaceWithZero = false);
  void emitLiteralPool(bool AtAreaEnd = false);
  void emitAllLiteralPools();
  void ensureDefaultSection();
  void emitDebugLocation();
  unsigned getDebugFileNumber(StringRef Path);
  void recordDebugSymbol(StringRef Name, MCSymbol *Symbol,
                         DebugSymbol::KindTy Kind, unsigned Type = 0);
  void emitNumericLabel(const NumericLocalLabel &Label);
  void emitPCLabel();
  std::string getNumericLabelSymbolName(unsigned Number) const;
  void emitDataValue(const MCExpr *Expr, unsigned Size, bool IsSymbolic,
                     SMLoc Loc);
  void emitAlignment(Align Alignment, std::optional<int64_t> Fill = {});
  bool defineCodeLabel(const SourceLine &Line, StringRef NameToken,
                       unsigned Size);
  void runTargetInstruction(const SourceLine &Line, StringRef Text,
                            size_t Offset);

  bool handleProcedureStart(const SourceLine &Line, StringRef Name);
  bool handleProcedureEnd(const SourceLine &Line, StringRef First,
                          StringRef Tail, StringRef AfterFirst);
  bool handleBranch(const SourceLine &Line, StringRef Text, StringRef First,
                    StringRef Second, StringRef Tail, StringRef AfterFirst);
  bool handleCommon(const SourceLine &Line, StringRef Tail);
  bool handleIncludeBinary(const SourceLine &Line, StringRef Tail);
  bool handleArea(const SourceLine &Line, StringRef Tail);
  bool handleAlign(const SourceLine &Line, StringRef Tail);
  bool handleMap(const SourceLine &Line, StringRef Tail);
  bool handleField(const SourceLine &Line, StringRef First,
                   StringRef AfterFirst, StringRef Tail, bool FirstWasDefined);
  bool handleRegisterAlias(const SourceLine &Line, char Class,
                           StringRef NameToken, StringRef TargetExpression);
  bool handleExport(const SourceLine &Line, StringRef Tail);
  bool handleImport(const SourceLine &Line, StringRef First, StringRef Tail);
  bool handleAlias(const SourceLine &Line, StringRef Tail);
  bool handleReloc(const SourceLine &Line, StringRef Tail);
  bool handleEqu(const SourceLine &Line, StringRef First,
                 StringRef AfterFirst);
  bool handleStorage(const SourceLine &Line, StringRef First,
                     StringRef Second, StringRef Tail, StringRef AfterFirst,
                     const std::optional<NumericLocalLabel> &NumericLabel);
  bool handleData(const SourceLine &Line, StringRef First, StringRef Second,
                  StringRef Tail, StringRef AfterFirst, bool UsesPC,
                  const std::optional<NumericLocalLabel> &NumericLabel);
  bool handleRout(const SourceLine &Line, StringRef First, StringRef Second,
                  StringRef Tail, StringRef AfterFirst);
  bool handleInstructionLine(const SourceLine &Line, StringRef Text,
                             StringRef Statement, StringRef First,
                             StringRef Second, StringRef Tail,
                             const std::optional<NumericLocalLabel>
                                 &NumericLabel);

  void beginFile();
  bool finishFile();
  void emitDebugInformation();

public:
  ARMASMParser(SourceMgr &SM, MCContext &Ctx, MCStreamer &Out,
               const MCAsmInfo &MAI, const ARMASMParserOptions &Opts)
      : MCAsmParser(Ctx, Out, SM, MAI), Opts(Opts),
        Traits(Ctx.getTargetTriple()) {
    Lexer.setLexARMASM(true);
  }

  //===--------------------------------------------------------------------===//
  // MCAsmParser interface
  //===--------------------------------------------------------------------===//

  void addDirectiveHandler(StringRef Directive,
                           ExtensionDirectiveHandler Handler) override {
    ExtensionDirectiveMap[Directive] = Handler;
  }

  void addAliasForDirective(StringRef, StringRef) override {}

  bool Run(bool NoInitialTextSection, bool NoFinalize = false) override;

  void setParsingMSInlineAsm(bool) override {}
  bool isParsingMSInlineAsm() override { return false; }
  bool isParsingARMASM() const override { return true; }
  const ARMASMParserOptions *getARMASMOptions() const override {
    return &Opts;
  }

  bool parseMSInlineAsm(std::string &, unsigned &, unsigned &,
                        SmallVectorImpl<std::pair<void *, bool>> &,
                        SmallVectorImpl<std::string> &,
                        SmallVectorImpl<std::string> &, const MCInstrInfo *,
                        MCInstPrinter *, MCAsmParserSemaCallback &) override {
    return true;
  }

  void Note(SMLoc L, const Twine &Msg, SMRange Range = {}) override {
    printMessage(L, SourceMgr::DK_Note, Msg, Range);
  }

  bool Warning(SMLoc L, const Twine &Msg, SMRange Range = {}) override {
    printMessage(L, SourceMgr::DK_Warning, Msg, Range);
    return false;
  }

  bool printError(SMLoc L, const Twine &Msg, SMRange Range = {}) override {
    HadError = true;
    printMessage(L, SourceMgr::DK_Error, Msg, Range);
    return true;
  }

  void printMessage(SMLoc L, SourceMgr::DiagKind Kind, const Twine &Msg,
                    SMRange Range) {
    // Map locations in substituted lines back to the source line.
    if (L.isValid()) {
      unsigned Buffer = SrcMgr.FindBufferContainingLoc(L);
      auto Synthetic = SyntheticBuffers.find(Buffer);
      if (Synthetic != SyntheticBuffers.end()) {
        const SourceLine &Line = Synthetic->second;
        StringRef Text = SrcMgr.getMemoryBuffer(Buffer)->getBuffer();
        int Column = L.getPointer() - Text.data();
        emitDiagnostic(SMDiagnostic(SrcMgr, L, Line.Filename, Line.Line,
                                    Column, Kind, Msg.str(), Text, {}, {}));
        reportMacroInvocations();
        return;
      }
    }
    SrcMgr.PrintMessage(L, Kind, Msg, Range);
    if (L.isValid())
      reportMacroInvocations();
  }

  const AsmToken &Lex() override {
    const AsmToken &Tok = Lexer.Lex();
    if (Tok.is(AsmToken::Error))
      Error(Lexer.getErrLoc(), Lexer.getErr());
    return Tok;
  }

  bool parseIdentifier(StringRef &Res) override {
    if (Lexer.isNot(AsmToken::Identifier) && Lexer.isNot(AsmToken::String))
      return true;
    Res = Lexer.is(AsmToken::String) ? getTok().getStringContents()
                                     : unquoteIdentifier(getTok().getString());
    Lex();
    return false;
  }

  StringRef parseStringToEndOfStatement() override {
    const char *Start = getTok().getLoc().getPointer();
    while (Lexer.isNot(AsmToken::EndOfStatement) && Lexer.isNot(AsmToken::Eof))
      Lexer.Lex();
    const char *End = getTok().getLoc().getPointer();
    return StringRef(Start, End - Start);
  }

  bool parseEscapedString(std::string &Data) override {
    if (check(getTok().isNot(AsmToken::String), "expected string"))
      return true;
    Data = getTok().getStringContents().str();
    Lex();
    return false;
  }

  bool parseAngleBracketString(std::string &) override { return true; }

  void eatToEndOfStatement() override {
    while (Lexer.isNot(AsmToken::EndOfStatement) && Lexer.isNot(AsmToken::Eof))
      Lexer.Lex();
    if (Lexer.is(AsmToken::EndOfStatement))
      Lexer.Lex();
  }

  bool parseExpression(const MCExpr *&Res, SMLoc &EndLoc) override {
    return parseExpressionImpl(Res, EndLoc, /*PrimaryOnly=*/false);
  }

  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc,
                        AsmTypeInfo *) override {
    return parseExpressionImpl(Res, EndLoc, /*PrimaryOnly=*/true);
  }

  bool parseParenExpression(const MCExpr *&Res, SMLoc &EndLoc) override {
    // The '(' has been consumed: parse to the matching ')'.
    if (parseExpression(Res, EndLoc))
      return true;
    EndLoc = getTok().getEndLoc();
    return parseRParen();
  }

  bool parseAbsoluteExpression(int64_t &Res) override {
    const MCExpr *Expr;
    SMLoc StartLoc = getTok().getLoc();
    SMLoc EndLoc;
    if (parseExpression(Expr, EndLoc))
      return true;
    if (!Expr->evaluateAsAbsolute(Res, getStreamer().getAssemblerPtr()))
      return Error(StartLoc, "expected absolute expression");
    return false;
  }

  bool checkForValidSection() override {
    if (!getStreamer().getCurrentFragment()) {
      ensureDefaultSection();
      return false;
    }
    return false;
  }

  bool parseExpressionImpl(const MCExpr *&Res, SMLoc &EndLoc,
                           bool PrimaryOnly) {
    const char *Start = getTok().getLoc().getPointer();
    StringRef Rest(Start, CurrentLexerBuffer.end() - Start);
    size_t LineEnd = Rest.find_first_of("\r\n");
    StringRef Text = stripComment(Rest.take_front(LineEnd));
    ExpressionParser Parser(Text, effectiveVariables(), *this, Opts.NoEscape,
                            /*AllowSymbolic=*/true, /*Partial=*/true);
    Expected<VariableValue> Value =
        PrimaryOnly ? Parser.parseOperand() : Parser.parse();
    if (!Value)
      return Error(getTok().getLoc(), toString(Value.takeError()));
    const char *End = Start + Parser.getPosition();
    // Reposition the lexer after the expression.
    Lexer.setBuffer(CurrentLexerBuffer, End);
    Lexer.Lex();
    EndLoc = SMLoc::getFromPointer(End);
    switch (Value->Kind) {
    case VariableKind::Arithmetic:
      Res = MCConstantExpr::create(static_cast<int64_t>(Value->Arithmetic),
                                   Ctx);
      return false;
    case VariableKind::Symbolic:
      Res = Value->Expr;
      return false;
    case VariableKind::Logical:
    case VariableKind::String:
    case VariableKind::RegisterRelative:
      return Error(SMLoc::getFromPointer(Start),
                   "A2062: illegal expression type; expected absolute "
                   "numeric or program relative");
    }
    llvm_unreachable("unknown ARMASM value kind");
  }

  //===--------------------------------------------------------------------===//
  // Dialect queries for target parsers
  //===--------------------------------------------------------------------===//

  bool lookUpARMASMRegisterAlias(StringRef Name, char &Class,
                                 unsigned &Number) const override {
    auto It = FinalRegisterAliases.find(Name);
    if (It == FinalRegisterAliases.end())
      return false;
    Class = It->second.Class;
    Number = It->second.Number;
    return true;
  }

  bool lookUpARMASMBasedField(StringRef Name, StringRef &BaseRegister,
                              int64_t &Offset) const override {
    auto It = FinalStorageMapFields.find(Name);
    if (It == FinalStorageMapFields.end() || !It->second.BaseRegister)
      return false;
    BaseRegister = *It->second.BaseRegister;
    Offset = It->second.Offset;
    return true;
  }
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Statement processing
//===----------------------------------------------------------------------===//

namespace {

unsigned getCOFFCharacteristics(StringRef SectionName, StringRef Flags,
                                bool IsARM) {
  enum {
    Alloc = 1 << 0,
    Code = 1 << 1,
    Load = 1 << 2,
    InitData = 1 << 3,
    NoWrite = 1 << 7,
  };
  unsigned SecFlags = 0;
  bool ReadOnlyRemoved = false;
  for (char Flag : Flags) {
    switch (Flag) {
    case 'b':
      SecFlags |= Alloc;
      SecFlags &= ~Load;
      break;
    case 'd':
      SecFlags |= InitData | Load;
      SecFlags &= ~NoWrite;
      break;
    case 'r':
      ReadOnlyRemoved = false;
      SecFlags |= NoWrite;
      if ((SecFlags & (Code | Alloc)) == 0)
        SecFlags |= InitData;
      if ((SecFlags & Alloc) == 0)
        SecFlags |= Load;
      break;
    case 'w':
      SecFlags &= ~NoWrite;
      ReadOnlyRemoved = true;
      break;
    case 'x':
      SecFlags |= Code | Load;
      if (!ReadOnlyRemoved)
        SecFlags |= NoWrite;
      break;
    default:
      llvm_unreachable("unexpected ARMASM section flag");
    }
  }
  if (SecFlags == 0)
    SecFlags = InitData;

  unsigned Characteristics = 0;
  if (SecFlags & Code)
    Characteristics |= COFF::IMAGE_SCN_CNT_CODE | COFF::IMAGE_SCN_MEM_EXECUTE;
  if (SecFlags & InitData)
    Characteristics |= COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
  if ((SecFlags & Alloc) && (SecFlags & Load) == 0)
    Characteristics |= COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA;
  if (MCSectionCOFF::isImplicitlyDiscardable(SectionName))
    Characteristics |= COFF::IMAGE_SCN_MEM_DISCARDABLE;
  Characteristics |= COFF::IMAGE_SCN_MEM_READ;
  if ((SecFlags & NoWrite) == 0)
    Characteristics |= COFF::IMAGE_SCN_MEM_WRITE;
  if ((Characteristics & COFF::IMAGE_SCN_CNT_CODE) && IsARM)
    Characteristics |= COFF::IMAGE_SCN_MEM_16BIT;
  return Characteristics;
}

enum class ExportKind { None, Data, Function };

struct ExportSpec {
  std::string Name;
  ExportKind Type = ExportKind::None;
};

Expected<ExportSpec> parseExportSpec(StringRef Text) {
  Text = Text.trim();
  if (Text.empty())
    return createStringError(inconvertibleErrorCode(),
                             "A2003: improper line syntax");

  StringRef Name = Text;
  ExportKind Type = ExportKind::None;
  if (size_t Open = Text.find('['); Open != StringRef::npos) {
    size_t Close = Text.rfind(']');
    if (Close == StringRef::npos || !Text.drop_front(Close + 1).trim().empty())
      return createStringError(inconvertibleErrorCode(),
                               "A2075: improper line syntax; expected: ']'");
    Name = Text.take_front(Open).trim();
    StringRef Attribute = Text.slice(Open + 1, Close).trim();
    if (Attribute.equals_insensitive("DATA"))
      Type = ExportKind::Data;
    else if (Attribute.equals_insensitive("FUNC"))
      Type = ExportKind::Function;
    else
      return createStringError(inconvertibleErrorCode(),
                               "A2039: " + Attribute +
                                   " attribute does not pertain to a "
                                   "relocatable module; ignored");
  } else if (Name.find_first_of(" \t") != StringRef::npos &&
             !(Name.starts_with("|") && Name.ends_with("|"))) {
    return createStringError(inconvertibleErrorCode(),
                             "A2003: improper line syntax");
  }

  Name = unquoteIdentifier(Name);
  if (Name.empty())
    return createStringError(inconvertibleErrorCode(),
                             "A2003: improper line syntax");
  return ExportSpec{Name.str(), Type};
}

bool ARMASMParser::processStatement(const SourceLine &Line, StringRef Text) {
  S.CurrentListingLine = S.ListingLine;
  S.CurrentFilename = sys::path::convert_to_slash(Line.Filename);
  S.CurrentLine = Line.Line;
  S.CurrentLoc = Line.getLoc();
  // Files named by #line directives appear in debug information.
  StringRef Statement = stripComment(Text).trim();
  if (takeToken(Statement).equals_insensitive("#line")) {
    takeToken(Statement);
    StringRef File = Statement.trim();
    if (File.consume_front("\"") && File.consume_back("\""))
      S.DebugFileSet.insert(File);
  }
  if (!emitting()) {
    collectStatement(Line, Text);
    return false;
  }
  return emitStatement(Line, Text);
}

/// Pass 1: record the definitions that ARMASM lets source use before they
/// appear, mirroring what armasm's first pass collects.
void ARMASMParser::collectStatement(const SourceLine &Line, StringRef Text) {
  StringRef Stripped = stripComment(Text);
  bool HasLabel = !Stripped.empty() && !isSpace(Stripped.front());
  StringRef Tail = Stripped.trim();
  StringRef First = takeToken(Tail);
  StringRef AfterFirst = Tail;
  StringRef Second = takeToken(AfterFirst);
  if (First.empty())
    return;

  // Numeric local labels, scoped as pass 2 scopes them: by AREA here, and by
  // ROUT in handleRout.
  if (First.equals_insensitive("AREA"))
    ++S.NumericLabelScope;
  else if (std::optional<NumericLocalLabel> Label =
               parseNumericLocalLabel(First))
    NumericLabelNames[S.NumericLabelScope * 100 + Label->Number].push_back(
        getNumericLabelSymbolName(Label->Number));

  if (First.equals_insensitive("EXPORT") ||
      First.equals_insensitive("GLOBAL")) {
    Expected<ExportSpec> Spec = parseExportSpec(Tail);
    if (Spec)
      CollectedExports[Spec->Name] = static_cast<ExportType>(Spec->Type);
    else
      consumeError(Spec.takeError());
    return;
  }

  if (std::optional<char> Class = Traits.aliasClassForDirective(Second)) {
    handleRegisterAlias(Line, *Class, First, AfterFirst);
    return;
  }
  if (First.equals_insensitive("MAP") || First == "^") {
    handleMap(Line, Tail);
    return;
  }

  auto Evaluate = [&](StringRef Expression) -> std::optional<VariableValue> {
    Expected<VariableValue> Value = evaluate(Expression);
    if (!Value) {
      consumeError(Value.takeError());
      return std::nullopt;
    }
    return std::move(*Value);
  };

  if (isEquDirective(Second)) {
    handleEqu(Line, First, AfterFirst);
    return;
  }

  bool IsField = First.equals_insensitive("FIELD") || First == "#" ||
                 Second.equals_insensitive("FIELD") || Second == "#";
  if (IsField) {
    handleField(Line, First, AfterFirst, Tail, /*FirstWasDefined=*/false);
    if (!HasLabel || First.equals_insensitive("FIELD") || First == "#")
      return;
  }
  if (First.equals_insensitive("ROUT") || Second.equals_insensitive("ROUT")) {
    handleRout(Line, First, Second, Tail, AfterFirst);
    if (!Second.equals_insensitive("ROUT"))
      return;
  }
  if (!HasLabel)
    return;

  // Symbol sizes for forward ?symbol references.
  StringRef Name = unquoteIdentifier(First);
  uint64_t Size = 0;
  if (std::optional<DataDirective> Directive = getDataDirective(Second)) {
    SmallVector<StringRef, 8> Operands;
    splitOperands(AfterFirst, Operands);
    for (StringRef Operand : Operands) {
      if (Directive->Kind == DataDirectiveKind::Byte)
        if (std::optional<VariableValue> Value = Evaluate(Operand))
          if (Value->Kind == VariableKind::String) {
            Size += Value->String.size();
            continue;
          }
      Size += Directive->size();
    }
  } else if (isStorageDirective(Second)) {
    SmallVector<StringRef, 3> Operands;
    splitOperands(AfterFirst, Operands);
    if (std::optional<VariableValue> Value = Evaluate(Operands.front()))
      if (Value->Kind == VariableKind::Arithmetic)
        Size = Value->Arithmetic;
  } else if (Second.equals_insensitive("FIELD")) {
    if (std::optional<VariableValue> Value = Evaluate(AfterFirst))
      if (Value->Kind == VariableKind::Arithmetic)
        Size = Value->Arithmetic;
  } else if (Second.equals_insensitive("ADRL")) {
    Size = 8;
  } else if (!Second.empty() && !Second.equals_insensitive("PROC") &&
             !Second.equals_insensitive("FUNCTION") &&
             !Second.equals_insensitive("ROUT")) {
    Size = 4;
  }
  CollectedSizes[Name] = Size;
  // A later line of this pass may already ask for the size (?label after an
  // instruction, as kxarm.h's PROLOG_NOP does); emission refines it.
  S.SymbolSizes.try_emplace(Name, Size);
}

void ARMASMParser::flushPending(bool ReplaceWithZero) {
  PendingEmission Pending = std::move(S.Pending);
  S.Pending = PendingEmission();
  switch (Pending.Kind) {
  case PendingEmission::None:
    return;
  case PendingEmission::Value:
    if (ReplaceWithZero)
      Out.emitIntValue(0, Pending.Size);
    else
      Out.emitValue(Pending.Expr, Pending.Size, Pending.Loc);
    return;
  case PendingEmission::Branch: {
    // {PC} in the branch names its own statement's location.
    MCSymbol *CurrentPCSymbol = S.CurrentPCSymbol;
    S.CurrentPCSymbol = Pending.PCSymbol;
    scope_exit RestorePC([&] { S.CurrentPCSymbol = CurrentPCSymbol; });
    if (ReplaceWithZero) {
      std::string Text = Pending.Mnemonic + " .";
      runTargetInstruction(Pending.Line, Text, 0);
    } else {
      runTargetInstruction(Pending.Line, Pending.Text,
                           Pending.InstructionOffset);
    }
    return;
  }
  }
}

/// Emit the current area's ldr= literal pool, with a line-table entry for
/// the statement that places it, as armasm does. END places pools at the end
/// of their areas, where armasm aligns them differently.
void ARMASMParser::emitLiteralPool(bool AtAreaEnd) {
  MCTargetStreamer *TS = Out.getTargetStreamer();
  if (!TS || !Out.getCurrentFragment() || !TS->hasCurrentConstantPool())
    return;
  emitDebugLocation();
  if (AtAreaEnd)
    TS->emitCurrentConstantPoolAtSectionEnd();
  else
    TS->emitCurrentConstantPool();
}

/// Emit the pending literal pool of every area at the end of its area.
void ARMASMParser::emitAllLiteralPools() {
  MCTargetStreamer *TS = Out.getTargetStreamer();
  // Without a current section nothing has been emitted, so no pool waits.
  if (!TS || !Out.getCurrentFragment())
    return;
  MCSection *Current = Out.getCurrentSectionOnly();
  std::optional<unsigned> CurrentDebugSection = S.CurrentDebugCodeSection;
  bool CurrentIsCode = S.CurrentAreaIsCode;
  for (AreaDefinition *Area : S.AreaOrder) {
    Out.switchSection(Area->Section);
    S.CurrentDebugCodeSection = Area->DebugCodeSection;
    S.CurrentAreaIsCode = Area->IsCode;
    emitLiteralPool(/*AtAreaEnd=*/true);
  }
  if (Current)
    Out.switchSection(Current);
  S.CurrentDebugCodeSection = CurrentDebugSection;
  S.CurrentAreaIsCode = CurrentIsCode;
}

void ARMASMParser::emitAlignment(Align Alignment, std::optional<int64_t> Fill) {
  MCSection *Section = Out.getCurrentSectionOnly();
  if (MAI.useCodeAlign(*Section) && !Fill)
    Out.emitCodeAlignment(Alignment, getTargetParser().getSTI(), 0);
  else
    Out.emitValueToAlignment(Alignment, Fill.value_or(0), 1, 0);
}

void ARMASMParser::ensureDefaultSection() {
  if (!S.CurrentAreaName.empty())
    return;
  S.CurrentAreaName = "__DefaultSection";
  S.CurrentAreaIsCode = false;
  S.CurrentAreaIsNoInit = false;
  S.CurrentAreaUsesCodeAlignment = false;
  S.CurrentAreaAlignment = 8;
  AreaDefinition &Definition = S.Areas["__DefaultSection{}"];
  Definition.Alignment = 8;
  Definition.AttributeSignature = "data,readwrite,align=3";
  Definition.Characteristics = getCOFFCharacteristics(
      "__DefaultSection", "dw", !Traits.is64Bit());
  Definition.Section =
      Ctx.getCOFFSection("__DefaultSection", Definition.Characteristics);
  Definition.BaseSymbol = Ctx.createTempSymbol();
  S.AreaOrder.push_back(&Definition);
  S.CurrentAreaBaseSymbol = Definition.BaseSymbol;
  Out.switchSection(Definition.Section);
  emitAlignment(Align(8));
  Out.emitLabel(Definition.BaseSymbol);
}

/// The CodeView file number of \p Path, assigning the next number (and
/// emitting its checksum) the first time a file is used. armasm numbers files
/// in the order they first appear in line tables.
unsigned ARMASMParser::getDebugFileNumber(StringRef Path) {
  auto [It, Inserted] =
      S.DebugFileNumbers.try_emplace(Path, S.DebugFileNumbers.size() + 1);
  if (!Inserted)
    return It->second;
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
      MemoryBuffer::getFile(Path, /*IsText=*/false);
  SmallVector<uint8_t, 32> Checksum;
  unsigned Kind = 0;
  if (Buffer) {
    StringRef Contents = (*Buffer)->getBuffer();
    ArrayRef<uint8_t> Bytes(reinterpret_cast<const uint8_t *>(Contents.data()),
                            Contents.size());
    if (Opts.DebugUseSHA1) {
      std::array<uint8_t, 20> Hash = SHA1::hash(Bytes);
      Checksum.assign(Hash.begin(), Hash.end());
      Kind = static_cast<unsigned>(codeview::FileChecksumKind::SHA1);
    } else {
      std::array<uint8_t, 32> Hash = SHA256::hash(Bytes);
      Checksum.assign(Hash.begin(), Hash.end());
      Kind = static_cast<unsigned>(codeview::FileChecksumKind::SHA256);
    }
  } else if (Path != "<stdin>") {
    // Files named only by #line: an all-zero checksum of no kind.
    Checksum.assign(32, 0);
  }
  void *Memory = Ctx.allocate(Checksum.size(), 1);
  memcpy(Memory, Checksum.data(), Checksum.size());
  Out.emitCVFileDirective(
      It->second, Path,
      ArrayRef(static_cast<const uint8_t *>(Memory), Checksum.size()), Kind);
  return It->second;
}

void ARMASMParser::emitDebugLocation() {
  if (!Opts.DebugInfo || !S.CurrentAreaIsCode || !S.CurrentDebugCodeSection)
    return;
  // Code from a macro expansion belongs to the outermost invocation.
  std::string Filename = S.CurrentFilename;
  unsigned LineNumber = S.CurrentLine;
  if (!S.MacroInvocations.empty()) {
    Filename = sys::path::convert_to_slash(S.MacroInvocations.front().Filename);
    LineNumber = S.MacroInvocations.front().Line;
  }
  std::string Path = normalizeDebugPath(Filename);
  if (const auto &Directive = S.ActiveLineDirective)
    if (Directive->PhysicalFile == Filename &&
        LineNumber > Directive->PhysicalLine) {
      LineNumber = Directive->Line + (LineNumber - Directive->PhysicalLine - 1);
      Path = Directive->Filename;
    }
  unsigned File = getDebugFileNumber(Path);
  DebugCodeSection &Section = S.DebugCodeSections[*S.CurrentDebugCodeSection];
  if (Section.HasLines && Section.LastFile == File &&
      Section.LastLine == LineNumber)
    return;
  Section.LastFile = File;
  Section.LastLine = LineNumber;
  Out.emitCVLocDirective(Section.FunctionId, File, LineNumber,
                         /*Column=*/0, /*PrologueEnd=*/false,
                         /*IsStmt=*/true, StringRef(), S.CurrentLoc);
  Section.HasLines = true;
}

/// Record a label for CodeView: S_LABEL32 for code labels, and S_LDATA32 or
/// S_GDATA32, typed by the defining directive, for data.
void ARMASMParser::recordDebugSymbol(StringRef Name, MCSymbol *Symbol,
                                     DebugSymbol::KindTy Kind, unsigned Type) {
  S.SawDebugSymbol = true;
  // armasm records nothing for the exception data in PDATA areas.
  if (!Opts.DebugInfo || S.CurrentAreaIsPData ||
      !S.DebugSymbolNames.insert(Name).second)
    return;
  S.DebugSymbols.push_back({Name.str(), Symbol, nullptr, Kind,
                            isExported(Name), Type, S.CurrentAreaBaseSymbol});
}

std::string ARMASMParser::getNumericLabelSymbolName(unsigned Number) const {
  std::string SymbolName;
  raw_string_ostream SymbolOS(SymbolName);
  SymbolOS << "_lc" << format("%03u", Number) << '_'
           << format("%06u", S.CurrentListingLine) << '_';
  return SymbolName;
}

void ARMASMParser::emitNumericLabel(const NumericLocalLabel &Label) {
  std::string SymbolName = getNumericLabelSymbolName(Label.Number);
  ++S.NumericLabelDefinitions[S.NumericLabelScope * 100 + Label.Number];
  MCSymbol *Symbol = getSymbol(SymbolName);
  defineCOFFSymbol(Symbol, COFF::IMAGE_SYM_CLASS_LABEL);
  emitLabel(Symbol, S.CurrentLoc);
  recordDebugSymbol(SymbolName, Symbol,
                    S.CurrentAreaIsCode ? DebugSymbol::Label
                                        : DebugSymbol::Data);
  // References (%f1, %b1) name the _lc symbol, so that relocations against
  // a numeric label use it, as armasm's do.
  Out.emitAssignment(Ctx.createDirectionalLocalSymbol(S.NumericLabelScope *
                                                          100 +
                                                      Label.Number),
                     MCSymbolRefExpr::create(Symbol, Ctx));
}

void ARMASMParser::emitPCLabel() {
  MCSymbol *Symbol = getSymbol("|" + utostr(S.PCSymbolCount++));
  defineCOFFSymbol(Symbol, COFF::IMAGE_SYM_CLASS_LABEL);
  emitLabel(Symbol, S.CurrentLoc);
  S.CurrentPCSymbol = Symbol;
  if (S.CurrentAreaIsCode)
    recordDebugSymbol(Symbol->getName(), Symbol, DebugSymbol::Label);
}

void ARMASMParser::emitDataValue(const MCExpr *Expr, unsigned Size,
                                 bool IsSymbolic, SMLoc Loc) {
  flushPending();
  if (IsSymbolic) {
    S.Pending.Kind = PendingEmission::Value;
    S.Pending.Expr = Expr;
    S.Pending.Size = Size;
    S.Pending.Loc = Loc;
  } else {
    int64_t IntValue = cast<MCConstantExpr>(Expr)->getValue();
    Out.emitIntValue(IntValue, Size);
  }
  S.PreviousData = PreviousDataDefinition{Size, IsSymbolic,
                                          /*IsInstruction=*/false};
}

void ARMASMParser::runTargetInstruction(const SourceLine &Line, StringRef Text,
                                        size_t Offset) {
  // Lex the instruction from the source buffer when the line was not
  // rewritten, and from a buffer holding the substituted line otherwise.
  StringRef Buffer;
  const char *Start;
  if (Text.data() == Line.Text.data() ||
      (Text == Line.Text && Line.Buffer.data())) {
    Buffer = Line.Buffer;
    Start = Line.Text.data() + Offset;
  } else {
    unsigned ID = SrcMgr.AddNewSourceBuffer(
        MemoryBuffer::getMemBufferCopy(Text, Line.Filename), SMLoc());
    SyntheticBuffers[ID] = Line;
    Buffer = SrcMgr.getMemoryBuffer(ID)->getBuffer();
    Start = Buffer.data() + Offset;
  }
  CurrentLexerBuffer = Buffer;
  Lexer.setBuffer(Buffer, Start);
  Lexer.Lex();

  AsmToken ID = getTok();
  SMLoc IDLoc = ID.getLoc();
  if (ID.isNot(AsmToken::Identifier)) {
    Error(IDLoc, "unexpected token at start of statement");
    return;
  }
  StringRef IDVal = ID.getString();
  Lex();
  checkForValidSection();

  std::string OpcodeStr = IDVal.lower();
  ParseInstructionInfo IInfo;
  SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> Operands;
  bool Failed =
      getTargetParser().parseInstruction(IInfo, OpcodeStr, ID, Operands);
  if (!Failed && !hasPendingError()) {
    unsigned Opcode = 0;
    uint64_t ErrorInfo;
    getTargetParser().matchAndEmitInstruction(IDLoc, Opcode, Operands, Out,
                                              ErrorInfo,
                                              /*MatchingInlineAsm=*/false);
  }
  printPendingErrors();
}

bool ARMASMParser::emitStatement(const SourceLine &Line, StringRef Text) {
  S.CurrentPCSymbol = nullptr;
  S.BuiltinVariables["VAR"] =
      VariableValue::arithmetic(S.CurrentStorageMap.Offset);

  StringRef Stripped = stripComment(Text);
  StringRef Statement = Stripped.trim();
  StringRef Tail = Statement;
  StringRef First = takeToken(Tail);
  StringRef AfterFirst = Tail;
  StringRef Second = takeToken(AfterFirst);
  // Only a token in column 1 is a label; otherwise the first token is the
  // opcode, and a directive name in second place is just an operand (as in
  // "BL proc").
  S.StartsWithLabel = !Stripped.empty() && !isSpace(Stripped.front());
  StringRef LabelDirective = S.StartsWithLabel ? Second : StringRef();

  bool FirstWasDefined = S.FirstWasDefined;
  S.FirstWasDefined = false;

  // #line N "file" attributes the following lines to another source in
  // debug information, as armasm does.
  if (First.equals_insensitive("#line")) {
    StringRef Operands = Tail;
    StringRef Number = takeToken(Operands);
    unsigned NewLine;
    StringRef File = Operands.trim();
    if (Number.getAsInteger(10, NewLine) || !File.consume_front("\"") ||
        !File.consume_back("\""))
      return fatal(Line, "A2003: improper line syntax");
    S.ActiveLineDirective = PassState::LineDirective{
        S.CurrentFilename, S.CurrentLine, File.str(), NewLine};
    return false;
  }

  bool IsRelocDirective = First.equals_insensitive("RELOC");
  if (!First.empty() && !IsRelocDirective) {
    flushPending();
    S.PreviousData.reset();
  }

  // Variable declarations and assignments were executed by assembly
  // control.
  if (First.equals_insensitive("GBLA") || First.equals_insensitive("GBLL") ||
      First.equals_insensitive("GBLS") || isSetDirective(Second))
    return false;

  if (std::optional<char> Class = Traits.aliasClassForDirective(LabelDirective))
    return handleRegisterAlias(Line, *Class, First, AfterFirst);
  if (LabelDirective.equals_insensitive("RN"))
    return fatal(Line, "A2034: unknown opcode: RN");

  if (First.equals_insensitive("MAP") || First == "^")
    return handleMap(Line, Tail);
  if (First.equals_insensitive("FIELD") || First == "#" ||
      Second.equals_insensitive("FIELD") || Second == "#")
    return handleField(Line, First, AfterFirst, Tail, FirstWasDefined);

  bool UsesPC = usesProgramCounter(Stripped);
  std::optional<NumericLocalLabel> NumericLabel =
      parseNumericLocalLabel(First);
  if (NumericLabel) {
    if (!NumericLabel->RoutName.empty() &&
        NumericLabel->RoutName != S.NumericLabelRoutName)
      return fatal(Line, "numeric local label ROUT name mismatch");
    S.DefinedNumericLabels.insert(NumericLabel->Number);
  }
  bool IsDataLine = isDataDirective(First) || isDataDirective(LabelDirective);
  bool IsStorageLine = isStorageDirective(First) || isStorageDirective(LabelDirective);
  if (UsesPC && !IsDataLine)
    emitPCLabel();

  if (First.equals_insensitive("EXPORT") ||
      First.equals_insensitive("GLOBAL"))
    return handleExport(Line, Tail);
  if (First.equals_insensitive("ALIAS"))
    return handleAlias(Line, Tail);
  if (First.equals_insensitive("IMPORT") || First.equals_insensitive("EXTERN"))
    return handleImport(Line, First, Tail);
  if (First.equals_insensitive("COMMON"))
    return handleCommon(Line, Tail);
  if (First.equals_insensitive("INCBIN"))
    return handleIncludeBinary(Line, Tail);
  if (IsRelocDirective)
    return handleReloc(Line, Tail);
  if (First.equals_insensitive("LTORG")) {
    if (!Tail.empty())
      return fatal(Line, "A2003: improper line syntax");
    emitLiteralPool();
    return false;
  }
  if (First.equals_insensitive("KEEP")) {
    SmallVector<StringRef, 2> Operands;
    splitOperands(Tail, Operands);
    if (Operands.size() != 1 || Operands[0].empty())
      return fatal(Line, "A2003: improper line syntax");
    return false;
  }
  if (First.equals_insensitive("END")) {
    if (S.ActiveProcedureArea)
      return fatal(Line, "A2057: missing ENDP directive in section " +
                             *S.ActiveProcedureArea);
    // armasm attributes the pools it places at END to the following line.
    ++S.CurrentLine;
    emitAllLiteralPools();
    --S.CurrentLine;
    return false;
  }

  // armasm's instruction set selection and stack alignment directives.
  if (!Traits.is64Bit()) {
    if (First.equals_insensitive("THUMB") || First.equals_insensitive("CODE16"))
      return false;
    if (First.equals_insensitive("ARM") || First.equals_insensitive("CODE32"))
      return fatal(Line, "ARM instructions are not supported; only Thumb-2 "
                         "code can be assembled for Windows on ARM");
    if (First.equals_insensitive("PRESERVE8") ||
        First.equals_insensitive("REQUIRE8"))
      return false;
  }
  if (First.equals_insensitive("AREA"))
    return handleArea(Line, Tail);
  if (isEquDirective(LabelDirective))
    return handleEqu(Line, First, AfterFirst);
  if (LabelDirective.equals_insensitive("PROC") ||
      LabelDirective.equals_insensitive("FUNCTION"))
    return handleProcedureStart(Line, First);
  if (First.equals_insensitive("ENDP") ||
      First.equals_insensitive("ENDFUNC") ||
      LabelDirective.equals_insensitive("ENDP") ||
      LabelDirective.equals_insensitive("ENDFUNC"))
    return handleProcedureEnd(Line, First, Tail, AfterFirst);
  if (IsStorageLine)
    return handleStorage(Line, First, Second, Tail, AfterFirst, NumericLabel);
  if (IsDataLine)
    return handleData(Line, First, Second, Tail, AfterFirst, UsesPC,
                      NumericLabel);
  if (First.equals_insensitive("ROUT") ||
      LabelDirective.equals_insensitive("ROUT"))
    return handleRout(Line, First, Second, Tail, AfterFirst);
  if (First.equals_insensitive("ALIGN"))
    return handleAlign(Line, Tail);
  if (isBranchMnemonic(First) || isBranchMnemonic(LabelDirective))
    return handleBranch(Line, Text, First, Second, Tail, AfterFirst);
  return handleInstructionLine(Line, Text, Stripped, First, Second, Tail,
                               NumericLabel);
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Directive handlers
//===----------------------------------------------------------------------===//

namespace {

bool ARMASMParser::defineCodeLabel(const SourceLine &Line, StringRef NameToken,
                                   unsigned Size) {
  StringRef Name = unquoteIdentifier(NameToken);
  if (conflictsWithObjectDefinition(Name))
    return symbolConflict(Line, Name);
  S.DefinedObjectSymbols.insert(Name);
  MCSymbol *Symbol = getSymbol(Name);
  if (S.AtProcedureStart)
    defineLocalSymbol(Name, Symbol, COFF::IMAGE_SYM_CLASS_LABEL,
                      COFF::IMAGE_SYM_DTYPE_FUNCTION
                          << COFF::SCT_COMPLEX_TYPE_SHIFT);
  else
    defineLocalSymbol(Name, Symbol, COFF::IMAGE_SYM_CLASS_LABEL);
  emitLabel(Symbol, Line.getLoc());
  recordDebugSymbol(Name, Symbol, DebugSymbol::Label);
  S.SymbolSizes[Name] = Size;
  return false;
}

bool ARMASMParser::handleProcedureStart(const SourceLine &Line,
                                        StringRef NameToken) {
  if (S.ActiveProcedureArea)
    return fatal(Line, "A2092: improper program syntax; missing ENDP "
                       "directive or nested function definition");
  StringRef Name = unquoteIdentifier(NameToken);
  if (conflictsWithObjectDefinition(Name))
    return symbolConflict(Line, Name);
  ensureDefaultSection();
  S.DefinedObjectSymbols.insert(Name);
  S.ActiveProcedureArea = S.CurrentAreaName;
  S.AtProcedureStart = true;
  MCSymbol *Symbol = getSymbol(Name);
  const int FunctionType = COFF::IMAGE_SYM_DTYPE_FUNCTION
                           << COFF::SCT_COMPLEX_TYPE_SHIFT;
  defineLocalSymbol(Name, Symbol, COFF::IMAGE_SYM_CLASS_LABEL,
                    Opts.DebugInfo ? std::optional<int>(FunctionType)
                                   : std::nullopt);
  if (isExported(Name) && Opts.DebugInfo) {
    defineCOFFSymbol(Symbol, COFF::IMAGE_SYM_CLASS_EXTERNAL, FunctionType);
    Out.emitSymbolAttribute(Symbol, MCSA_Global);
  }
  emitLabel(Symbol, Line.getLoc());
  S.ActiveDebugProcedure.reset();
  if (Opts.DebugInfo && S.CurrentAreaIsCode &&
      S.DebugSymbolNames.insert(Name).second) {
    S.SawDebugSymbol = true;
    S.ActiveDebugProcedure = S.DebugSymbols.size();
    S.DebugSymbols.push_back({Name.str(), Symbol, Ctx.createTempSymbol(),
                              DebugSymbol::Procedure, isExported(Name)});
    S.DebugSymbols.back().Line = S.CurrentLine;
  }
  return false;
}

bool ARMASMParser::handleProcedureEnd(const SourceLine &Line, StringRef First,
                                      StringRef Tail, StringRef AfterFirst) {
  StringRef EndTail = First.equals_insensitive("ENDP") ||
                              First.equals_insensitive("ENDFUNC")
                          ? Tail
                          : AfterFirst;
  if (!EndTail.empty())
    return fatal(Line, "A2003: improper line syntax: " + EndTail);
  if (!S.ActiveProcedureArea)
    return fatal(Line,
                 "A2093: improper program syntax; unexpected ENDP directive");
  if (S.ActiveDebugProcedure) {
    MCSymbol *End = S.DebugSymbols[*S.ActiveDebugProcedure].EndSymbol;
    Out.emitLabel(End);
    // The procedure, and any alias of it made inside it, get .bf and .ef
    // symbols as well.
    for (DebugSymbol &Procedure : S.DebugSymbols)
      if (Procedure.Kind == DebugSymbol::Procedure &&
          Procedure.EndSymbol == End) {
        Procedure.EndLine = S.CurrentLine;
        registerDebugFunction(Procedure);
      }
  }
  S.ActiveDebugProcedure.reset();
  S.ActiveProcedureArea.reset();
  S.AtProcedureStart = false;
  return false;
}

bool ARMASMParser::handleBranch(const SourceLine &Line, StringRef Text,
                                StringRef First, StringRef Second,
                                StringRef Tail, StringRef AfterFirst) {
  ensureDefaultSection();
  bool HasLabel = S.StartsWithLabel && isBranchMnemonic(Second);
  StringRef Mnemonic = HasLabel ? Second : First;
  StringRef TargetText = HasLabel ? AfterFirst : Tail;
  SmallVector<StringRef, 2> Operands;
  splitOperands(TargetText, Operands);
  if (Operands.size() != 1 || Operands[0].empty())
    return fatal(Line, "A2003: improper line syntax");

  if (HasLabel && defineCodeLabel(Line, First, 4))
    return true;

  emitDebugLocation();
  S.Pending.Kind = PendingEmission::Branch;
  S.Pending.Line = Line;
  S.Pending.Text = Text.str();
  S.Pending.InstructionOffset = Mnemonic.data() - Text.data();
  S.Pending.Mnemonic = Mnemonic.str();
  S.Pending.PCSymbol = S.CurrentPCSymbol;
  S.PreviousData = PreviousDataDefinition{4, /*IsSymbolic=*/true,
                                          /*IsInstruction=*/true};
  S.AtProcedureStart = false;
  return false;
}

bool ARMASMParser::handleCommon(const SourceLine &Line, StringRef Tail) {
  SmallVector<StringRef, 3> Operands;
  splitOperands(Tail, Operands);
  if (Operands.empty() || Operands[0].empty())
    return fatal(Line, "A2003: improper line syntax: End of line");
  if (Operands.size() > 2)
    return fatal(Line, "A2221: The COMMON directive takes two parameters; "
                       "specifying an alignment is not supported");

  StringRef Name = unquoteIdentifier(Operands[0]);
  if (Name.empty() || hasInternalSymbol(Name) ||
      S.DefinedObjectSymbols.contains(Name) ||
      S.ExternalSymbols.contains(Name) || S.CommonSymbols.contains(Name))
    return symbolConflict(Line, Name);
  uint64_t Size = 0;
  if (Operands.size() == 2 && evaluateAbsolute(Line, Operands[1], Size))
    return true;
  int64_t SignedSize = static_cast<int64_t>(Size);
  if (SignedSize < 0 || Size > UINT32_MAX)
    return fatal(Line,
                 "A2209: Immediate value " + Twine(SignedSize) + " out of range");
  S.CommonSymbols.insert(Name);
  ensureDefaultSection();
  MCSymbol *Symbol = getSymbol(Name);
  if (Size == 0)
    Out.emitSymbolAttribute(Symbol, MCSA_Global);
  else
    Out.emitCommonSymbol(Symbol, Size, Align(1));
  return false;
}

bool ARMASMParser::handleIncludeBinary(const SourceLine &Line,
                                       StringRef Tail) {
  StringRef IncludedFilename = takeToken(Tail);
  if (IncludedFilename.empty() || !Tail.empty())
    return fatal(Line, "A2003: improper line syntax");
  SmallVector<SmallString<256>, 4> Candidates;
  if (sys::path::is_absolute(IncludedFilename)) {
    Candidates.emplace_back(IncludedFilename);
  } else {
    SmallString<256> Path(sys::path::parent_path(S.CurrentFilename));
    sys::path::append(Path, IncludedFilename);
    Candidates.push_back(Path);
    Candidates.emplace_back(IncludedFilename);
    for (StringRef IncludeDir : Opts.IncludeDirs) {
      Path = IncludeDir;
      sys::path::append(Path, IncludedFilename);
      Candidates.push_back(Path);
    }
  }
  std::error_code EC =
      std::make_error_code(std::errc::no_such_file_or_directory);
  std::unique_ptr<MemoryBuffer> Contents;
  for (const SmallString<256> &Candidate : Candidates) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
        MemoryBuffer::getFile(Candidate, /*IsText=*/false);
    if (Buffer) {
      Contents = std::move(*Buffer);
      break;
    }
    EC = Buffer.getError();
  }
  if (!Contents)
    return fatal(Line, "unable to open include file '" + IncludedFilename +
                           "': " + EC.message());
  ensureDefaultSection();
  Out.emitBytes(Contents->getBuffer());
  return false;
}

bool ARMASMParser::handleArea(const SourceLine &Line, StringRef Tail) {
  SmallVector<StringRef, 8> Attributes;
  splitOperands(Tail, Attributes);
  if (Attributes.front().empty())
    return fatal(Line, "A2003: improper line syntax");
  if (llvm::any_of(ArrayRef(Attributes).drop_front(),
                   [](StringRef Attribute) { return Attribute.empty(); }))
    return fatal(Line, "A2146: illegal symbol ,; AREA attribute expected");
  Expected<AreaName> Area = parseAreaName(Attributes.front());
  if (!Area)
    return fatal(Line, toString(Area.takeError()));
  bool IsCode = false;
  bool IsData = false;
  bool IsReadOnly = false;
  bool IsReadWrite = false;
  bool IsNoInit = false;
  bool IsPData = false;
  bool UsesCodeAlignment = false;
  unsigned Alignment = Traits.getDefaultAreaAlignment();
  std::optional<AreaName> AssociativeArea;
  for (StringRef Attribute : ArrayRef(Attributes).drop_front()) {
    Attribute = Attribute.trim();
    if (Attribute.equals_insensitive("CODE")) {
      IsCode = true;
    } else if (Attribute.equals_insensitive("DATA")) {
      IsData = true;
    } else if (Attribute.equals_insensitive("READWRITE")) {
      IsReadWrite = true;
    } else if (Attribute.equals_insensitive("READONLY")) {
      IsReadOnly = true;
    } else if (Attribute.equals_insensitive("NOINIT")) {
      IsNoInit = true;
    } else if (Attribute.equals_insensitive("PDATA")) {
      IsPData = true;
    } else if (Attribute.equals_insensitive("CODEALIGN")) {
      UsesCodeAlignment = true;
    } else if (Attribute.equals_insensitive("COMDEF") ||
               Attribute.equals_insensitive("COMMON")) {
      statementWarning(Line, 4039,
                       Attribute + " attribute does not pertain to a "
                                   "relocatable module; ignored");
    } else if (Attribute.consume_front_insensitive("ALIGN=")) {
      uint64_t Value;
      if (evaluateAbsolute(Line, Attribute, Value))
        return true;
      if (Value > 31)
        return fatal(Line,
                     "A2209: Immediate value " + Attribute + " out of range");
      Alignment = Value;
    } else if (Attribute.consume_front_insensitive("ASSOC=")) {
      Expected<AreaName> Associated = parseAreaName(Attribute);
      if (!Associated)
        return fatal(Line, toString(Associated.takeError()));
      AssociativeArea = *Associated;
    } else {
      return fatal(Line, "A2041: unknown section flag: " + Attribute);
    }
  }

  if ((IsCode && (IsData || IsNoInit)) || (IsReadOnly && IsReadWrite))
    statementWarning(
        Line, 4172,
        "illegal combination of section flags: section flags can not be "
        "inferred, code and data/uninitialized, readonly/readwrite");

  bool IsWritable = IsReadWrite || (!IsReadOnly && !IsCode && !IsPData);
  StringRef Flags;
  if (IsCode)
    Flags = IsWritable ? "xrw" : "xr";
  else if (IsNoInit)
    Flags = IsWritable ? "bw" : "br";
  else
    Flags = IsWritable ? "dw" : "dr";
  std::string ComdatSymbol;
  COFF::COMDATType Selection = static_cast<COFF::COMDATType>(0);
  std::string SectionSuffix;
  if (AssociativeArea) {
    if (AssociativeArea->ComdatSymbol.empty())
      return fatal(Line, "A2003: improper line syntax");
    ComdatSymbol = AssociativeArea->ComdatSymbol.str();
    Selection = COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE;
    SectionSuffix = ",associative," + ComdatSymbol;
  } else if (!Area->ComdatSymbol.empty()) {
    ComdatSymbol = Area->ComdatSymbol.str();
    Selection = COFF::IMAGE_COMDAT_SELECT_NODUPLICATES;
    SectionSuffix = ",one_only," + ComdatSymbol;
  }
  std::string AreaKey =
      (Twine(Area->Name) + "{" + Area->ComdatSymbol + "}").str();
  std::string AttributeSignature =
      (Twine(IsCode) + "," + Twine(IsReadOnly) + "," + Twine(IsReadWrite) +
       "," + Twine(IsNoInit) + "," + Twine(IsPData) + "," +
       Twine(UsesCodeAlignment) + "," + Twine(Alignment) + "," +
       SectionSuffix)
          .str();
  auto [AreaEntry, IsNewArea] = S.Areas.try_emplace(AreaKey);
  AreaDefinition &Definition = AreaEntry->second;
  if (IsNewArea) {
    Definition.Alignment = 1ULL << Alignment;
    Definition.UsesCodeAlignment = UsesCodeAlignment;
    Definition.IsCode = IsCode;
    Definition.IsNoInit = IsNoInit;
    // armasm keeps exception data out of CodeView, whether the area is marked
    // PDATA or merely named .pdata.
    Definition.IsPData = IsPData || Area->Name == ".pdata";
    Definition.Characteristics =
        getCOFFCharacteristics(Area->Name, Flags, !Traits.is64Bit());
    if (Selection)
      Definition.Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
    Definition.ComdatSymbol = ComdatSymbol;
    Definition.Selection = Selection;
    Definition.AttributeSignature = AttributeSignature;
    Definition.Section =
        Ctx.getCOFFSection(Area->Name, Definition.Characteristics,
                           Definition.ComdatSymbol, Definition.Selection);
    Definition.BaseSymbol = Ctx.createTempSymbol();
    S.AreaOrder.push_back(&Definition);
    // armasm writes the names of COMDAT areas ahead of the other symbols.
    if (!Area->ComdatSymbol.empty() && Out.isObj() &&
        Ctx.getObjectFileType() == MCContext::IsCOFF)
      static_cast<MCWinCOFFStreamer &>(Out).getWriter().addARMASMCOMDATKey(
          getSymbol(Area->ComdatSymbol));
  } else if (Definition.AttributeSignature != AttributeSignature) {
    statementWarning(Line, 4043, "redefinition of section flags ignored");
  }
  S.CurrentAreaIsCode = Definition.IsCode;
  S.CurrentAreaIsNoInit = Definition.IsNoInit;
  S.CurrentAreaIsPData = Definition.IsPData;
  S.CurrentAreaName = Area->Name.str();
  S.CurrentAreaAlignment = Definition.Alignment;
  S.CurrentAreaUsesCodeAlignment = Definition.UsesCodeAlignment;
  S.CurrentAreaBaseSymbol = Definition.BaseSymbol;
  Out.switchSection(Definition.Section);
  ++S.NumericLabelScope;
  S.NumericLabelRoutName.clear();
  S.DefinedNumericLabels.clear();
  if (IsNewArea) {
    emitAlignment(Align(1ULL << Alignment));
    Out.emitLabel(Definition.BaseSymbol);
  }
  S.CurrentDebugCodeSection.reset();
  if (Opts.DebugInfo && S.CurrentAreaIsCode) {
    auto Existing = S.DebugCodeSectionIndices.find(AreaKey);
    if (Existing == S.DebugCodeSectionIndices.end()) {
      unsigned Index = S.DebugCodeSections.size();
      S.DebugCodeSections.push_back({Definition.Section,
                                     Definition.BaseSymbol,
                                     Ctx.createTempSymbol(), Index});
      S.DebugCodeSectionIndices[AreaKey] = Index;
      S.CurrentDebugCodeSection = Index;
      Definition.DebugCodeSection = Index;
      Out.emitCVFuncIdDirective(Index);
    } else {
      S.CurrentDebugCodeSection = Existing->second;
    }
  }
  return false;
}

bool ARMASMParser::handleAlign(const SourceLine &Line, StringRef Tail) {
  SmallVector<StringRef, 4> Operands;
  splitOperands(Tail, Operands);
  if (Operands.size() > 4 ||
      llvm::any_of(ArrayRef(Operands).drop_front(),
                   [](StringRef Operand) { return Operand.empty(); }))
    return fatal(Line, "A2003: improper line syntax");

  uint64_t Alignment = 4;
  uint64_t Offset = 0;
  uint64_t Fill = 0;
  uint64_t FillSize = 1;
  bool HasFill = Operands.size() >= 3;
  if (!Operands.front().empty() &&
      evaluateAbsolute(Line, Operands[0], Alignment))
    return true;
  if (!isPowerOf2_64(Alignment) || Alignment > (1ULL << 31))
    return fatal(Line,
                 "A2209: Immediate value " + Twine(Alignment) + " out of range");
  if (Operands.size() >= 2 && evaluateAbsolute(Line, Operands[1], Offset))
    return true;
  if (HasFill) {
    if (evaluateAbsolute(Line, Operands[2], Fill))
      return true;
    FillSize = S.CurrentAreaIsCode ? Traits.getCodeFillSize() : 1;
  } else if (S.CurrentAreaIsCode && S.CurrentAreaUsesCodeAlignment) {
    std::tie(Fill, FillSize) = Traits.getCodeAlignmentFill();
  }
  if (Operands.size() == 4 && evaluateAbsolute(Line, Operands[3], FillSize))
    return true;
  if (FillSize != 1 && FillSize != 2 && FillSize != 4)
    return fatal(Line, "A2197: value size must be 1, 2, or 4");
  if (!S.CurrentAreaBaseSymbol)
    return fatal(Line, "A2088: no current AREA");
  if (Alignment > S.CurrentAreaAlignment)
    statementWarning(
        Line, 4228,
        "Alignment value exceeds AREA alignment; alignment not guaranteed");

  // ((Offset - (Here - AreaBase)) & (Alignment - 1)) bytes of padding.
  MCSymbol *Here = Ctx.createTempSymbol();
  Out.emitLabel(Here);
  const MCExpr *Distance = MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(Here, Ctx),
      MCSymbolRefExpr::create(S.CurrentAreaBaseSymbol, Ctx), Ctx);
  const MCExpr *Padding = MCBinaryExpr::createAnd(
      MCBinaryExpr::createSub(MCConstantExpr::create(Offset, Ctx), Distance,
                              Ctx),
      MCConstantExpr::create(Alignment - 1, Ctx), Ctx);
  if (FillSize != 1) {
    int64_t Remainder;
    const MCExpr *Check = MCBinaryExpr::createMod(
        Padding, MCConstantExpr::create(FillSize, Ctx), Ctx);
    if (!Check->evaluateAsAbsolute(Remainder, Out.getAssemblerPtr()))
      return fatal(Line, "expected absolute expression");
    if (Remainder != 0)
      return fatal(Line, "A2226: The given alignment pad doesn't evenly "
                         "divide the number of padding bytes");
  }
  // armasm attributes padding to the ALIGN line; no padding, no line.
  int64_t PaddingSize;
  if (!Padding->evaluateAsAbsolute(PaddingSize, Out.getAssemblerPtr()) ||
      PaddingSize != 0)
    emitDebugLocation();
  Out.emitFill(*MCBinaryExpr::createDiv(
                   Padding, MCConstantExpr::create(FillSize, Ctx), Ctx),
               FillSize, Fill, Line.getLoc());
  return false;
}

bool ARMASMParser::handleMap(const SourceLine &Line, StringRef Tail) {
  SmallVector<StringRef, 2> Operands;
  splitOperands(Tail, Operands);
  if (Operands.empty() || Operands.size() > 2 || Operands[0].empty() ||
      (Operands.size() == 2 && Operands[1].empty()))
    return statementError(Line, "A2003: improper line syntax");
  uint64_t Offset;
  if (evaluateAbsolute(Line, Operands[0], Offset))
    return true;
  S.CurrentStorageMap.Offset = static_cast<int64_t>(Offset);
  S.CurrentStorageMap.BaseRegister =
      Operands.size() == 2
          ? std::optional<std::string>(Operands[1].trim().str())
          : std::nullopt;
  S.BuiltinVariables["VAR"] = VariableValue::arithmetic(Offset);
  return false;
}

bool ARMASMParser::handleField(const SourceLine &Line, StringRef First,
                               StringRef AfterFirst, StringRef Tail,
                               bool FirstWasDefined) {
  bool HasLabel = !First.equals_insensitive("FIELD") && First != "#";
  StringRef Expression = HasLabel ? AfterFirst : Tail;
  uint64_t Size;
  if (evaluateAbsolute(Line, Expression, Size))
    return true;
  int64_t SignedSize = static_cast<int64_t>(Size);
  if (SignedSize < 0)
    return statementError(Line, "A2209: Immediate value " + Twine(SignedSize) +
                                    " out of range");

  if (HasLabel) {
    StringRef Name = unquoteIdentifier(First);
    if (!isValidVariableName(First) || hasInternalSymbol(Name) ||
        S.Variables.contains(Name) || S.Constants.contains(Name) ||
        FirstWasDefined || Traits.isPredefinedRegisterName(Name))
      return symbolConflict(Line, Name);
    S.StorageMapFields[Name] = S.CurrentStorageMap;
    S.SymbolSizes[Name] = Size;
    if (S.CurrentStorageMap.BaseRegister)
      if (std::optional<unsigned> Base = Traits.getStorageMapBaseEncoding(
              *S.CurrentStorageMap.BaseRegister))
        S.RegisterRelativeValues[Name] =
            VariableValue::registerRelative(*Base, 0);
  }
  if (S.CurrentStorageMap.Offset > INT64_MAX - SignedSize)
    return statementError(Line, "storage map offset overflow");
  S.CurrentStorageMap.Offset += SignedSize;
  S.BuiltinVariables["VAR"] =
      VariableValue::arithmetic(S.CurrentStorageMap.Offset);
  return false;
}

bool ARMASMParser::handleRegisterAlias(const SourceLine &Line, char Class,
                                       StringRef NameToken,
                                       StringRef TargetExpression) {
  StringRef Name = unquoteIdentifier(NameToken);
  if (!isValidVariableName(NameToken) || TargetExpression.empty())
    return statementError(Line, "A2173: syntax error in expression");
  auto Existing = S.RegisterAliases.find(Name);
  if (S.Variables.contains(Name) || S.Constants.contains(Name) ||
      S.DefinedSymbols.contains(Name) || S.StorageMapFields.contains(Name) ||
      (Existing != S.RegisterAliases.end() && Existing->second.Class != Class))
    return symbolConflict(Line, Name);
  if (std::optional<std::pair<char, unsigned>> Register =
          Traits.parseAliasableRegister(Name)) {
    if (Register->first != Class)
      return symbolConflict(Line, Name);
  } else if (Traits.isPredefinedRegisterName(Name)) {
    return symbolConflict(Line, Name);
  }

  // The target is another alias of the same class, a register of the class
  // or a register number.
  TargetExpression = TargetExpression.trim();
  unsigned Number;
  auto Alias = S.RegisterAliases.find(unquoteIdentifier(TargetExpression));
  std::optional<std::pair<char, unsigned>> Register =
      Traits.parseAliasableRegister(TargetExpression);
  if (!Register)
    Register = Traits.parseCoreRegisterAlias(TargetExpression);
  if (Alias != S.RegisterAliases.end()) {
    if (Alias->second.Class != Class)
      return statementError(Line, "A2046: unknown or bad symbol type: " +
                                      TargetExpression);
    Number = Alias->second.Number;
  } else if (Register) {
    if (Register->first != Class)
      return statementError(Line, "A2046: unknown or bad symbol type: " +
                                      TargetExpression);
    Number = Register->second;
  } else {
    VariableMap EmptyVariables;
    ExpressionParser Parser(TargetExpression, EmptyVariables, *this,
                            Opts.NoEscape);
    Expected<VariableValue> Value = Parser.parse();
    if (!Value || Value->Kind != VariableKind::Arithmetic) {
      if (!Value)
        consumeError(Value.takeError());
      return statementError(Line, TargetExpression.contains('.')
                                      ? "A2003: improper line syntax"
                                      : "A2046: unknown or bad symbol type: " +
                                            TargetExpression);
    }
    unsigned Maximum = Traits.maxRegisterNumber(Class);
    if (Value->Arithmetic > Maximum)
      return statementError(
          Line, "A2069: immediate value " +
                    Twine(static_cast<int64_t>(Value->Arithmetic)) +
                    " out of range; expected values: [0,1,.." +
                    Twine(Maximum) + "]");
    Number = Value->Arithmetic;
  }

  S.RegisterAliases[Name] = RegisterAlias{Class, Number};
  return false;
}

bool ARMASMParser::handleExport(const SourceLine &Line, StringRef Tail) {
  Expected<ExportSpec> Spec = parseExportSpec(Tail);
  if (!Spec)
    return fatal(Line, toString(Spec.takeError()));
  StringRef Name = Spec->Name;
  if (hasInternalSymbol(Name))
    return symbolConflict(Line, Name);
  S.ExportLocations[Name] = {Line.getLoc(), {S.CurrentFilename, S.CurrentLine}};
  MCSymbol *Symbol = getSymbol(Name);
  // [FUNC] makes the symbol a function; otherwise it keeps the type its
  // definition gave it (a procedure under -g is a function even if exported
  // with [DATA]).
  int Type = 0;
  if (Spec->Type == ExportKind::Function)
    Type = COFF::IMAGE_SYM_DTYPE_FUNCTION << COFF::SCT_COMPLEX_TYPE_SHIFT;
  else if (auto It = S.LocalSymbolClasses.find(Name);
           It != S.LocalSymbolClasses.end() && It->second.Type)
    Type = *It->second.Type;
  defineCOFFSymbol(Symbol, COFF::IMAGE_SYM_CLASS_EXTERNAL, Type);
  if (Symbol->isTemporary())
    return Error(Line.getLoc(), "non-local symbol required");
  Out.emitSymbolAttribute(Symbol, MCSA_Global);
  return false;
}

bool ARMASMParser::handleAlias(const SourceLine &Line, StringRef Tail) {
  SmallVector<StringRef, 2> Operands;
  splitOperands(Tail, Operands);
  if (Operands.size() != 2 || Operands[0].empty() || Operands[1].empty())
    return fatal(Line, "A2003: improper line syntax");
  StringRef Original = unquoteIdentifier(Operands[0]);
  StringRef Alias = unquoteIdentifier(Operands[1]);
  if (!S.DefinedObjectSymbols.contains(Original))
    return fatal(Line, "A2249: symbol '" + Original +
                           "' is undefined or external");
  if (Alias.empty() || S.DefinedObjectSymbols.contains(Alias) ||
      conflictsWithObjectDefinition(Alias))
    return symbolConflict(Line, Alias);
  S.DefinedObjectSymbols.insert(Alias);
  MCSymbol *AliasSymbol = getSymbol(Alias);
  // An alias has the storage class and type of the symbol it names, as that
  // symbol would have if it were not exported.
  auto Class = S.LocalSymbolClasses.find(Original);
  int StorageClass = Class != S.LocalSymbolClasses.end()
                         ? Class->second.StorageClass
                         : COFF::IMAGE_SYM_CLASS_STATIC;
  std::optional<int> Type = Class != S.LocalSymbolClasses.end()
                                ? Class->second.Type
                                : std::nullopt;
  defineLocalSymbol(Alias, AliasSymbol, StorageClass, Type);
  if (isExported(Alias) && Type)
    defineCOFFSymbol(AliasSymbol, COFF::IMAGE_SYM_CLASS_EXTERNAL, Type);
  Out.emitAssignment(AliasSymbol,
                     MCSymbolRefExpr::create(getSymbol(Original), Ctx));
  // An alias gets the same debug record as the symbol it names, and the
  // alias of a procedure is described as a procedure too.
  if (Opts.DebugInfo && !S.DebugSymbolNames.contains(Alias)) {
    auto It = llvm::find_if(S.DebugSymbols, [&](const DebugSymbol &Symbol) {
      return Symbol.Name == Original;
    });
    if (It != S.DebugSymbols.end()) {
      DebugSymbol Copy = *It;
      Copy.Name = Alias.str();
      Copy.Symbol = AliasSymbol;
      Copy.IsExternal = isExported(Alias);
      S.DebugSymbolNames.insert(Alias);
      S.DebugSymbols.push_back(std::move(Copy));
      if (S.DebugSymbols.back().Kind == DebugSymbol::Procedure &&
          S.DebugSymbols.back().EndLine)
        registerDebugFunction(S.DebugSymbols.back());
    }
  }
  return false;
}

/// Describe a procedure (or an alias of one) to the object writer, which
/// writes .bf and .ef symbols for it as armasm does under -g.
void ARMASMParser::registerDebugFunction(const DebugSymbol &Procedure) {
  if (Out.isObj() && Ctx.getObjectFileType() == MCContext::IsCOFF)
    static_cast<MCWinCOFFStreamer &>(Out).getWriter().addARMASMFunction(
        {Procedure.Symbol, Procedure.EndSymbol, Procedure.Line,
         Procedure.EndLine});
}

bool ARMASMParser::handleImport(const SourceLine &Line, StringRef First,
                                StringRef Tail) {
  bool Conditional = First.equals_insensitive("EXTERN");
  SmallVector<StringRef, 4> Operands;
  splitOperands(Tail, Operands);
  if (Operands.empty() || Operands.size() > 3 || Operands[0].empty())
    return fatal(Line, "A2003: improper line syntax");

  // armasm ignores a bracketed attribute after the name, with a warning.
  StringRef NameOperand = Operands[0].trim();
  if (!NameOperand.starts_with("|"))
    if (size_t Open = NameOperand.find('['); Open != StringRef::npos) {
      statementWarning(
          Line, 4039,
          "[ attribute does not pertain to a relocatable module; ignored");
      NameOperand = NameOperand.take_front(Open).trim();
    }
  StringRef Name = unquoteIdentifier(NameOperand);
  if (Name.empty() || hasInternalSymbol(Name) ||
      S.DefinedObjectSymbols.contains(Name) || S.CommonSymbols.contains(Name) ||
      S.ExternalSymbols.contains(Name))
    return symbolConflict(Line, Name);
  S.ExternalSymbols.insert(Name);

  if (Operands.size() == 1) {
    // Unlike IMPORT, an unused EXTERN does not appear in the object.
    if (!Conditional)
      Out.emitSymbolAttribute(getSymbol(Name), MCSA_Global);
    return false;
  }

  StringRef WeakOperand = Operands[1].trim();
  StringRef Weak = takeToken(WeakOperand);
  StringRef Fallback = unquoteIdentifier(takeToken(WeakOperand));
  if (!Weak.equals_insensitive("WEAK"))
    return fatal(Line, "A2135: attribute does not pertain to a relocatable "
                       "module; ignored, weak expected");
  if (Fallback.empty() || !WeakOperand.empty())
    return fatal(Line, "A2003: improper line syntax");
  if (Fallback == Name || hasInternalSymbol(Fallback) ||
      S.DefinedObjectSymbols.contains(Fallback) ||
      S.CommonSymbols.contains(Fallback))
    return symbolConflict(Line, Fallback);

  uint64_t Search = COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS;
  if (Operands.size() == 3) {
    StringRef TypeOperand = Operands[2].trim();
    StringRef Type = takeToken(TypeOperand);
    if (!Type.equals_insensitive("TYPE") || TypeOperand.empty())
      return fatal(Line, "A2003: improper line syntax");
    if (evaluateAbsolute(Line, TypeOperand, Search))
      return true;
    if (Search < COFF::IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY ||
        Search > COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS)
      statementWarning(Line, 4069,
                       "immediate value " + Twine(static_cast<int64_t>(Search)) +
                           " out of range; expected values: 1,2,3");
    Search &= 7;
  }

  S.ExternalSymbols.insert(Fallback);
  Out.emitSymbolAttribute(getSymbol(Fallback), MCSA_Global);
  S.PendingWeakExternals.push_back(
      {Name.str(), Fallback.str(), Search, Conditional});
  return false;
}

bool ARMASMParser::handleReloc(const SourceLine &Line, StringRef Tail) {
  SmallVector<StringRef, 2> Operands;
  splitOperands(Tail, Operands);
  if (Operands.empty() || Operands.size() > 2 || Operands[0].empty() ||
      (Operands.size() == 2 && Operands[1].empty()))
    return fatal(Line, "A2003: improper line syntax");
  if (!S.PreviousData)
    return fatal(Line, "A2203: RELOC directive must follow an instruction or "
                       "data definition directive");

  uint64_t Type;
  if (evaluateAbsolute(Line, Operands[0], Type))
    return true;
  StringRef RelocationName = Traits.getRelocationName(Type);
  if (RelocationName.empty())
    return fatal(Line, "A2204: Unknown relocation type " + Twine(Type));
  unsigned RelocationSize = Traits.getRelocationSize(Type);
  if (!S.PreviousData->IsInstruction && S.PreviousData->Size < RelocationSize)
    statementWarning(Line, 4205,
                     "Previous data definition too small for requested "
                     "relocation; emitting anyway");

  const MCExpr *Expr = nullptr;
  if (Operands.size() == 2) {
    Expected<VariableValue> Value =
        evaluate(Operands[1], /*AllowSymbolic=*/true);
    if (!Value)
      return fatal(Line, toString(Value.takeError()));
    if (Value->Kind == VariableKind::Arithmetic)
      Expr = MCConstantExpr::create(Value->Arithmetic, Ctx);
    else if (Value->Kind == VariableKind::Symbolic)
      Expr = Value->Expr;
    else
      return fatal(Line, "expression must be relocatable");
  } else if (S.PreviousData->IsSymbolic) {
    if (S.Pending.Kind == PendingEmission::Value) {
      Expr = S.Pending.Expr;
    } else {
      // A branch: its target is the relocation target.
      StringRef Target = StringRef(S.Pending.Text)
                             .drop_front(S.Pending.InstructionOffset +
                                         S.Pending.Mnemonic.size());
      Target = stripComment(Target).trim();
      Expected<VariableValue> Value = evaluate(Target, /*AllowSymbolic=*/true);
      if (!Value)
        return fatal(Line, toString(Value.takeError()));
      Expr = Value->Kind == VariableKind::Symbolic
                 ? Value->Expr
                 : MCConstantExpr::create(Value->Arithmetic, Ctx);
    }
  } else {
    return fatal(Line, "A2206: Must specify a relocation target");
  }
  MCValue Relocatable;
  if (!Expr->evaluateAsRelocatable(Relocatable, nullptr))
    return fatal(Line, "expression must be relocatable");

  unsigned Size = S.PreviousData->Size;
  flushPending(/*ReplaceWithZero=*/S.PreviousData->IsSymbolic);
  MCSymbol *Here = Ctx.createTempSymbol();
  Out.emitLabel(Here);
  const MCExpr *Offset = MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(Here, Ctx), MCConstantExpr::create(Size, Ctx),
      Ctx);
  Out.emitRelocDirective(*Offset, RelocationName, Expr, Line.getLoc());
  S.PreviousData.reset();
  return false;
}

bool ARMASMParser::handleEqu(const SourceLine &Line, StringRef First,
                             StringRef AfterFirst) {
  StringRef Name = unquoteIdentifier(First);
  if (conflictsWithObjectDefinition(Name))
    return symbolConflict(Line, Name);
  S.DefinedObjectSymbols.insert(Name);
  S.DefinedSymbols.insert(Name);
  std::string AssemblerName =
      isExported(Name) ? Name.str() : (".Larmasm$" + Name).str();
  S.ConstantSymbols[Name] = AssemblerName;

  Expected<VariableValue> Value = evaluate(AfterFirst);
  if (Value && Value->Kind == VariableKind::Arithmetic)
    S.Constants[Name] = Value->Arithmetic;
  else if (!Value)
    consumeError(Value.takeError());
  if (!emitting())
    return false;

  Expected<VariableValue> Symbolic =
      evaluate(AfterFirst, /*AllowSymbolic=*/true);
  if (!Symbolic)
    return fatal(Line, toString(Symbolic.takeError()));
  const MCExpr *Expr;
  if (Symbolic->Kind == VariableKind::Arithmetic)
    Expr = MCConstantExpr::create(Symbolic->Arithmetic, Ctx);
  else if (Symbolic->Kind == VariableKind::Symbolic)
    Expr = Symbolic->Expr;
  else
    return fatal(Line, "A2062: illegal expression type; expected absolute "
                       "numeric or program relative");
  Out.emitAssignment(getSymbol(AssemblerName), Expr);
  return false;
}

bool ARMASMParser::handleStorage(
    const SourceLine &Line, StringRef First, StringRef Second, StringRef Tail,
    StringRef AfterFirst, const std::optional<NumericLocalLabel> &NumericLabel) {
  ensureDefaultSection();
  bool HasLabel = !isStorageDirective(First);
  StringRef Directive = HasLabel ? Second : First;
  StringRef Values = HasLabel ? AfterFirst : Tail;
  SmallVector<StringRef, 3> Operands;
  splitOperands(Values, Operands);
  bool IsFill = Directive.equals_insensitive("FILL");
  if (Operands.empty() || Operands[0].empty() ||
      (!IsFill && Operands.size() != 1) ||
      (IsFill && (Operands.size() > 3 ||
                  llvm::any_of(Operands, [](StringRef Operand) {
                    return Operand.empty();
                  }))))
    return fatal(Line, "A2173: syntax error in expression");

  uint64_t Count;
  if (evaluateAbsolute(Line, Operands[0], Count))
    return true;
  int64_t SignedCount = static_cast<int64_t>(Count);
  if (SignedCount < 0)
    return fatal(Line,
                 "A2209: Immediate value " + Twine(SignedCount) + " out of range");

  uint64_t Value = 0;
  uint64_t ValueSize = 1;
  if (Operands.size() >= 2 && evaluateAbsolute(Line, Operands[1], Value))
    return true;
  if (Operands.size() == 3 && evaluateAbsolute(Line, Operands[2], ValueSize))
    return true;
  if (ValueSize != 1 && ValueSize != 2 && ValueSize != 4)
    return fatal(Line, "A2197: value size for FILL directive must be 1, 2, or 4");
  if (Count % ValueSize)
    return fatal(Line, "A2219: Fill size must be a multiple of value size");
  uint64_t MaxValue =
      ValueSize == 4 ? UINT32_MAX : (1ULL << (ValueSize * 8)) - 1;
  if (Value > MaxValue)
    return fatal(Line, "A2209: Immediate value " +
                           Twine(static_cast<int64_t>(Value)) +
                           " out of range");
  if (S.CurrentAreaIsNoInit && Value != 0)
    return fatal(Line,
                 "A2048: initialized data in an uninitialized data section");

  if (HasLabel) {
    StringRef Name = unquoteIdentifier(First);
    if (!NumericLabel) {
      if (conflictsWithObjectDefinition(Name))
        return symbolConflict(Line, Name);
      S.DefinedObjectSymbols.insert(Name);
      MCSymbol *Symbol = getSymbol(Name);
      defineLocalSymbol(Name, Symbol, COFF::IMAGE_SYM_CLASS_STATIC);
      emitLabel(Symbol, Line.getLoc());
      recordDebugSymbol(Name, Symbol, DebugSymbol::Data);
      S.SymbolSizes[Name] = Count;
    } else {
      emitNumericLabel(*NumericLabel);
    }
  }
  if (Count)
    emitDebugLocation();
  if (Value == 0)
    Out.emitFill(*MCConstantExpr::create(Count, Ctx), 0, Line.getLoc());
  else
    Out.emitFill(*MCConstantExpr::create(Count / ValueSize, Ctx), ValueSize,
                 Value, Line.getLoc());
  return false;
}

bool ARMASMParser::handleData(
    const SourceLine &Line, StringRef First, StringRef Second, StringRef Tail,
    StringRef AfterFirst, bool UsesPC,
    const std::optional<NumericLocalLabel> &NumericLabel) {
  ensureDefaultSection();
  bool HasLabel = !isDataDirective(First);
  StringRef Directive = HasLabel ? Second : First;
  StringRef Values = HasLabel ? AfterFirst : Tail;
  std::optional<DataDirective> Data = getDataDirective(Directive);
  assert(Data && "expected a data directive");
  if (!Data->MicrosoftCompatible)
    statementWarning(Line, 2034,
                     "unknown opcode: " + Directive +
                         "; accepted as an LLVM extension");
  // armasm attributes the alignment padding to the data's line.
  emitDebugLocation();
  if (!Data->Unaligned && Data->alignment() != 1)
    emitAlignment(Align(Data->alignment()),
                  Traits.padsDataWithCode() ? std::nullopt
                                            : std::optional<int64_t>(0));
  unsigned DataSize = Data->size();

  std::optional<std::string> DataLabel;
  if (HasLabel) {
    StringRef Name = unquoteIdentifier(First);
    if (!NumericLabel) {
      if (conflictsWithObjectDefinition(Name))
        return symbolConflict(Line, Name);
      S.DefinedObjectSymbols.insert(Name);
      MCSymbol *Symbol = getSymbol(Name);
      defineLocalSymbol(Name, Symbol, COFF::IMAGE_SYM_CLASS_STATIC);
      emitLabel(Symbol, Line.getLoc());
      recordDebugSymbol(Name, Symbol, DebugSymbol::Data,
                        getCodeViewDataType(*Data));
      DataLabel = Name.str();
    } else {
      emitNumericLabel(*NumericLabel);
    }
  }
  if (UsesPC)
    emitPCLabel();

  SmallVector<StringRef, 8> Operands;
  splitOperands(Values, Operands);
  if (Operands.empty() || llvm::any_of(Operands, [](StringRef Operand) {
        return Operand.empty();
      }))
    return fatal(Line, "A2173: syntax error in expression");

  uint64_t EmittedSize = 0;
  for (StringRef Operand : Operands) {
    SMLoc OperandLoc = Line.getLoc();
    if (Data->isFloatingPoint()) {
      bool IsSingle = Data->Kind == DataDirectiveKind::Single;
      APFloat Float(IsSingle ? APFloat::IEEEsingle() : APFloat::IEEEdouble());
      APFloat::opStatus Status;
      Expected<VariableValue> Value = evaluate(Operand);
      if (Value) {
        if (Value->Kind != VariableKind::Arithmetic)
          return fatal(Line, "A2062: illegal expression type; expected "
                             "absolute numeric");
        Status = Float.convertFromAPInt(APInt(64, Value->Arithmetic),
                                        /*IsSigned=*/true,
                                        APFloat::rmNearestTiesToEven);
      } else {
        consumeError(Value.takeError());
        Expected<APFloat::opStatus> Converted =
            Float.convertFromString(Operand, APFloat::rmNearestTiesToEven);
        if (!Converted) {
          consumeError(Converted.takeError());
          return fatal(Line, "A2173: syntax error in expression");
        }
        Status = *Converted;
      }
      if (Status & APFloat::opOverflow)
        return fatal(Line, IsSingle ? "A2022: Floating point value can not be "
                                      "represented in single precision"
                                    : "A2220: Floating point value out of "
                                      "range");
      emitDataValue(
          MCConstantExpr::create(Float.bitcastToAPInt().getZExtValue(), Ctx),
          DataSize, /*IsSymbolic=*/false, OperandLoc);
      EmittedSize += DataSize;
      continue;
    }

    // Values that depend on the location counter or on symbols are emitted
    // as relocatable expressions.
    Expected<VariableValue> Value = evaluate(Operand);
    if (!Value || (UsesPC && usesProgramCounter(Operand))) {
      if (!Value) {
        std::string Message = toString(Value.takeError());
        if (!StringRef(Message).starts_with("unknown variable or constant '") &&
            !Operand.contains('|') && !usesProgramCounter(Operand) &&
            !Operand.contains('%'))
          return fatal(Line, Message);
      }
      if (Data->Kind == DataDirectiveKind::Byte && !usesProgramCounter(Operand))
        return fatal(Line, "A2065: illegal expression type; expected absolute "
                           "numeric or string");
      if (Data->Kind == DataDirectiveKind::Word && !usesProgramCounter(Operand))
        return fatal(Line,
                     "A2061: illegal expression type; expected absolute numeric");
      flushPending();
      Expected<VariableValue> Symbolic =
          evaluate(Operand, /*AllowSymbolic=*/true);
      if (!Symbolic)
        return fatal(Line, toString(Symbolic.takeError()));
      const MCExpr *Expr =
          Symbolic->Kind == VariableKind::Symbolic
              ? Symbolic->Expr
              : MCConstantExpr::create(Symbolic->Arithmetic, Ctx);
      emitDataValue(Expr, DataSize, /*IsSymbolic=*/true, OperandLoc);
      EmittedSize += DataSize;
      continue;
    }
    if (Value->Kind == VariableKind::String) {
      if (Data->Kind != DataDirectiveKind::Byte)
        return fatal(Line,
                     "A2062: illegal expression type; expected absolute numeric");
      if (!Value->String.empty()) {
        flushPending();
        for (char Byte : Value->String)
          Out.emitIntValue(static_cast<unsigned char>(Byte), 1);
        S.PreviousData = PreviousDataDefinition{
            static_cast<unsigned>(Value->String.size()), /*IsSymbolic=*/false,
            /*IsInstruction=*/false};
      }
      EmittedSize += Value->String.size();
      continue;
    }
    if (Value->Kind != VariableKind::Arithmetic)
      return fatal(Line,
                   "A2062: illegal expression type; expected absolute numeric");

    if (!isIntegerValueInRange(Value->Arithmetic, Data->minimumValue(),
                               Data->maximumValue()))
      return fatal(Line, "A2209: Immediate value " +
                             Twine(static_cast<int64_t>(Value->Arithmetic)) +
                             " out of range");
    emitDataValue(
        MCConstantExpr::create(static_cast<int64_t>(Value->Arithmetic), Ctx),
        DataSize, /*IsSymbolic=*/false, OperandLoc);
    EmittedSize += DataSize;
  }
  if (DataLabel)
    S.SymbolSizes[*DataLabel] = EmittedSize;
  S.AtProcedureStart = false;
  return false;
}

bool ARMASMParser::handleRout(const SourceLine &Line, StringRef First,
                              StringRef Second, StringRef Tail,
                              StringRef AfterFirst) {
  StringRef RoutTail = First.equals_insensitive("ROUT") ? Tail : AfterFirst;
  if (!RoutTail.empty())
    return statementError(Line, "A2003: improper line syntax");
  if (Second.equals_insensitive("ROUT") && emitting()) {
    ensureDefaultSection();
    StringRef Name = unquoteIdentifier(First);
    if (conflictsWithObjectDefinition(Name) ||
        S.DefinedObjectSymbols.contains(Name))
      return symbolConflict(Line, Name);
    S.DefinedObjectSymbols.insert(Name);
    MCSymbol *Symbol = getSymbol(Name);
    defineLocalSymbol(Name, Symbol, COFF::IMAGE_SYM_CLASS_STATIC);
    Out.emitAssignment(Symbol,
                       MCSymbolRefExpr::create(S.CurrentAreaBaseSymbol, Ctx));
    S.SymbolSizes[Name] = 0;
  }
  ++S.NumericLabelScope;
  S.NumericLabelRoutName =
      First.equals_insensitive("ROUT") ? std::string() : First.str();
  S.DefinedNumericLabels.clear();
  return false;
}

bool ARMASMParser::handleInstructionLine(
    const SourceLine &Line, StringRef Text, StringRef Statement,
    StringRef First, StringRef Second, StringRef Tail,
    const std::optional<NumericLocalLabel> &NumericLabel) {
  if (!First.empty())
    ensureDefaultSection();
  bool HasLabel = !Statement.empty() && !isSpace(Statement.front());
  bool IsADRL = First.equals_insensitive("ADRL") ||
                Second.equals_insensitive("ADRL");
  if (HasLabel) {
    StringRef Name = unquoteIdentifier(First);
    if (!NumericLabel) {
      if (conflictsWithObjectDefinition(Name))
        return symbolConflict(Line, Name);
      S.DefinedObjectSymbols.insert(Name);
      MCSymbol *Symbol = getSymbol(Name);
      // Labels on instructions, and bare labels in code, are code labels.
      int StorageClass = !Second.empty() || S.CurrentAreaIsCode
                             ? COFF::IMAGE_SYM_CLASS_LABEL
                             : COFF::IMAGE_SYM_CLASS_STATIC;
      if (S.CurrentAreaIsCode && S.AtProcedureStart)
        defineLocalSymbol(Name, Symbol, StorageClass,
                          COFF::IMAGE_SYM_DTYPE_FUNCTION
                              << COFF::SCT_COMPLEX_TYPE_SHIFT);
      else
        defineLocalSymbol(Name, Symbol, StorageClass);
      emitLabel(Symbol, Line.getLoc());
      // A label on its own line names data in data areas.
      recordDebugSymbol(Name, Symbol,
                        Second.empty() && !S.CurrentAreaIsCode
                            ? DebugSymbol::Data
                            : DebugSymbol::Label);
    } else {
      emitNumericLabel(*NumericLabel);
    }
    if (!Second.empty()) {
      emitDebugLocation();
      // armasm's ?label of an instruction is its encoded size: 2 or 4 in
      // Thumb-2 code.
      MCFragment *Fragment = Out.getCurrentFragment();
      size_t Before = Fragment ? Fragment->getFixedSize() : 0;
      runTargetInstruction(Line, Text, Second.data() - Text.data());
      if (!NumericLabel) {
        uint64_t Size = IsADRL ? 8 : 4;
        if (!IsADRL && Fragment && Out.getCurrentFragment() == Fragment &&
            Fragment->getFixedSize() > Before)
          Size = Fragment->getFixedSize() - Before;
        S.SymbolSizes[Name] = Size;
      }
    }
  } else if (!First.empty()) {
    emitDebugLocation();
    runTargetInstruction(Line, Text, First.data() - Text.data());
  }
  if ((HasLabel && !Second.empty()) || (!HasLabel && !First.empty())) {
    S.PreviousData = PreviousDataDefinition{4, /*IsSymbolic=*/false,
                                            /*IsInstruction=*/true};
    S.AtProcedureStart = false;
  }
  return false;
}

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// Driving the passes
//===----------------------------------------------------------------------===//

namespace {

void ARMASMParser::beginFile() {
  // armasm records the absolute path of the source in a .file symbol.
  SmallString<256> Source(
      SrcMgr.getMemoryBuffer(SrcMgr.getMainFileID())->getBufferIdentifier());
  if (Source != "<stdin>") {
    sys::fs::make_absolute(Source);
    sys::path::native(Source);
    sys::path::remove_dots(Source, /*remove_dot_dot=*/true);
  }
  Out.emitFileDirective(Source);

  MCSymbol *CompId = getSymbol("@comp.id");
  defineCOFFSymbol(CompId, COFF::IMAGE_SYM_CLASS_STATIC);
  Out.emitAssignment(CompId, MCConstantExpr::create(Traits.getCompId(), Ctx));
  MCSymbol *Features = getSymbol("@feat.00");
  defineCOFFSymbol(Features, COFF::IMAGE_SYM_CLASS_STATIC);
  Out.emitAssignment(Features, MCConstantExpr::create(16, Ctx));

  if (Opts.DebugInfo)
    for (const std::string &File : DebugFileOrder)
      getDebugFileNumber(File);
}

void ARMASMParser::emitDebugInformation() {
  // armasm always writes .debug$S with S_OBJNAME and S_COMPILE3; without -g
  // the compile record carries the NoDbgInfo flag and nothing else follows.
  if (Opts.DebugInfo)
    for (const DebugCodeSection &Section : S.DebugCodeSections) {
      Out.switchSection(Section.Section);
      Out.emitLabel(Section.EndSymbol);
    }

  auto Difference = [&](MCSymbol *End, MCSymbol *Begin) {
    return MCBinaryExpr::createSub(MCSymbolRefExpr::create(End, Ctx),
                                   MCSymbolRefExpr::create(Begin, Ctx), Ctx);
  };
  auto EmitString = [&](StringRef Value) {
    Out.emitBytes(Value);
    Out.emitBytes(StringRef("\0", 1));
  };
  auto BeginSubsection = [&](codeview::DebugSubsectionKind Kind) {
    MCSymbol *Begin = Ctx.createTempSymbol();
    MCSymbol *End = Ctx.createTempSymbol();
    Out.emitIntValue(static_cast<unsigned>(Kind), 4);
    Out.emitValue(Difference(End, Begin), 4);
    Out.emitLabel(Begin);
    return End;
  };
  // Symbol records are zero-padded to four bytes; the record length includes
  // the padding.
  auto BeginRecord = [&](codeview::SymbolKind Kind) {
    MCSymbol *Begin = Ctx.createTempSymbol();
    MCSymbol *End = Ctx.createTempSymbol();
    Out.emitValue(Difference(End, Begin), 2);
    Out.emitLabel(Begin);
    Out.emitIntValue(static_cast<unsigned>(Kind), 2);
    return End;
  };
  auto EndRecord = [&](MCSymbol *End) {
    Out.emitValueToAlignment(Align(4), 0, 1, 0);
    Out.emitLabel(End);
  };

  MCSection *Symbols = Ctx.getCOFFSection(
      ".debug$S", getCOFFCharacteristics(".debug$S", "dr", false));
  Out.switchSection(Symbols);
  emitAlignment(Align(4));
  Out.emitIntValue(COFF::DEBUG_SECTION_MAGIC, 4);

  MCSymbol *CompilerEnd = BeginSubsection(codeview::DebugSubsectionKind::Symbols);
  MCSymbol *ObjectEnd = BeginRecord(codeview::SymbolKind::S_OBJNAME);
  Out.emitIntValue(0, 4);
  EmitString(Opts.DebugObjectFilename);
  EndRecord(ObjectEnd);
  MCSymbol *CompileEnd = BeginRecord(codeview::SymbolKind::S_COMPILE3);
  unsigned Flags = static_cast<unsigned>(codeview::SourceLanguage::Masm);
  if (!Opts.DebugInfo)
    Flags |= static_cast<unsigned>(codeview::CompileSym3Flags::NoDbgInfo);
  Out.emitIntValue(Flags, 4);
  Out.emitIntValue(Traits.getCodeViewMachine(), 2);
  for (unsigned I = 0; I != 4; ++I)
    Out.emitIntValue(0, 2);
  Out.emitIntValue(LLVM_VERSION_MAJOR, 2);
  Out.emitIntValue(LLVM_VERSION_MINOR, 2);
  Out.emitIntValue(LLVM_VERSION_PATCH, 2);
  Out.emitIntValue(0, 2);
  EmitString(Traits.is64Bit() ? "LLVM ARMASM64 Assembler " LLVM_VERSION_STRING
                              : "LLVM ARMASM Assembler " LLVM_VERSION_STRING);
  EndRecord(CompileEnd);
  Out.emitLabel(CompilerEnd);

  if (!Opts.DebugInfo)
    return;

  if (!Opts.DebugSourceLink.empty()) {
    // The record armasm64 emits for a Source Link file, referring to the
    // LF_STRING_ID in .debug$T.
    Out.emitIntValue(
        static_cast<unsigned>(codeview::DebugSubsectionKind::Symbols), 4);
    Out.emitIntValue(8, 4);
    Out.emitIntValue(6, 2);
    Out.emitIntValue(0x117e, 2);
    Out.emitIntValue(0x1002, 4);
    emitAlignment(Align(4));
  }

  Out.emitCVStringTableDirective();
  Out.emitCVFileChecksumsDirective();
  for (const DebugCodeSection &Section : S.DebugCodeSections)
    if (Section.HasLines)
      Out.emitCVLinetableDirective(Section.FunctionId, Section.StartSymbol,
                                   Section.EndSymbol);

  // All labels, data and procedures share one symbols subsection, which is
  // present (possibly empty) when the source defines any label.
  if (S.SawDebugSymbol) {
    MCSymbol *SymbolsEnd =
        BeginSubsection(codeview::DebugSubsectionKind::Symbols);
    // armasm writes the records in the order of its symbol hash table: by
    // bucket, and within a bucket the most recently mentioned name first.
    SmallVector<const DebugSymbol *, 16> Records;
    for (const DebugSymbol &Symbol : S.DebugSymbols)
      Records.push_back(&Symbol);
    auto RecordKey = [&](const DebugSymbol *Symbol) {
      unsigned Mention = S.SymbolMentions.lookup(Symbol->Name);
      return std::make_pair(getARMASMSymbolBucket(Symbol->Name), ~Mention);
    };
    llvm::stable_sort(Records, [&](const DebugSymbol *A, const DebugSymbol *B) {
      return RecordKey(A) < RecordKey(B);
    });
    for (const DebugSymbol *Record : Records) {
      const DebugSymbol &Symbol = *Record;
      switch (Symbol.Kind) {
      case DebugSymbol::Procedure: {
        MCSymbol *End = BeginRecord(Symbol.IsExternal
                                        ? codeview::SymbolKind::S_GPROC32
                                        : codeview::SymbolKind::S_LPROC32);
        Out.emitIntValue(0, 4); // Parent
        Out.emitIntValue(0, 4); // End
        Out.emitIntValue(0, 4); // Next
        Out.emitValue(Difference(Symbol.EndSymbol, Symbol.Symbol), 4);
        Out.emitIntValue(0, 4); // DbgStart
        Out.emitValue(Difference(Symbol.EndSymbol, Symbol.Symbol), 4);
        Out.emitIntValue(0x1001, 4); // LF_PROCEDURE in .debug$T
        Out.emitCOFFSecRel32(Symbol.Symbol, 0);
        Out.emitCOFFSectionIndex(Symbol.Symbol);
        Out.emitIntValue(0, 1);
        EmitString(Symbol.Name);
        EndRecord(End);
        MCSymbol *ScopeEnd = BeginRecord(codeview::SymbolKind::S_END);
        EndRecord(ScopeEnd);
        break;
      }
      case DebugSymbol::Label: {
        MCSymbol *End = BeginRecord(codeview::SymbolKind::S_LABEL32);
        Out.emitCOFFSecRel32(Symbol.Symbol, 0);
        Out.emitCOFFSectionIndex(Symbol.SectionSymbol ? Symbol.SectionSymbol
                                                      : Symbol.Symbol);
        Out.emitIntValue(0, 1);
        EmitString(Symbol.Name);
        EndRecord(End);
        break;
      }
      case DebugSymbol::Data: {
        MCSymbol *End = BeginRecord(Symbol.IsExternal
                                        ? codeview::SymbolKind::S_GDATA32
                                        : codeview::SymbolKind::S_LDATA32);
        Out.emitIntValue(Symbol.Type, 4);
        Out.emitCOFFSecRel32(Symbol.Symbol, 0);
        Out.emitCOFFSectionIndex(Symbol.SectionSymbol ? Symbol.SectionSymbol
                                                      : Symbol.Symbol);
        EmitString(Symbol.Name);
        EndRecord(End);
        break;
      }
      }
    }
    Out.emitLabel(SymbolsEnd);
  }

  // .debug$T: an LF_ARGLIST and LF_PROCEDURE for the procedures, and the
  // Source Link LF_STRING_ID.
  MCSection *Types = Ctx.getCOFFSection(
      ".debug$T", getCOFFCharacteristics(".debug$T", "dr", false));
  Out.switchSection(Types);
  emitAlignment(Align(4));
  Out.emitIntValue(COFF::DEBUG_SECTION_MAGIC, 4);
  Out.emitIntValue(6, 2);
  Out.emitIntValue(static_cast<unsigned>(codeview::LF_ARGLIST), 2);
  Out.emitIntValue(0, 4);
  Out.emitIntValue(14, 2);
  Out.emitIntValue(static_cast<unsigned>(codeview::LF_PROCEDURE), 2);
  Out.emitIntValue(3, 4);
  Out.emitIntValue(0, 1);
  Out.emitIntValue(0, 1);
  Out.emitIntValue(0, 2);
  Out.emitIntValue(0x1000, 4);
  if (!Opts.DebugSourceLink.empty()) {
    MCSymbol *Begin = Ctx.createTempSymbol();
    MCSymbol *End = Ctx.createTempSymbol();
    Out.emitValue(Difference(End, Begin), 2);
    Out.emitLabel(Begin);
    Out.emitIntValue(static_cast<unsigned>(codeview::LF_STRING_ID), 2);
    Out.emitIntValue(0, 4);
    EmitString(Opts.DebugSourceLink);
    Out.emitLabel(End);
  }
}

bool ARMASMParser::finishFile() {
  flushPending();
  emitAllLiteralPools();

  if (S.ActiveProcedureArea)
    return fatalFile(S.CurrentFilename, S.CurrentLine,
                     "A2057: missing ENDP directive in section " +
                         *S.ActiveProcedureArea);

  for (const auto &Export : S.ExportLocations)
    if (!S.DefinedObjectSymbols.contains(Export.first()) &&
        !S.CommonSymbols.contains(Export.first()))
      return fatalFile(Export.second.second.first,
                       Export.second.second.second,
                       "A2023: undefined symbol: " + Export.first());

  emitDebugInformation();

  for (const PendingWeakExternal &Weak : S.PendingWeakExternals) {
    if (Weak.Conditional && !S.ReferencedNames.contains(Weak.Name))
      continue;
    MCSymbol *Symbol = getSymbol(Weak.Name);
    Out.emitWeakReference(Symbol, getSymbol(Weak.Fallback));
    static_cast<MCSymbolCOFF *>(Symbol)->setWeakExternalCharacteristics(
        static_cast<COFF::WeakExternalCharacteristics>(Weak.Search));
  }
  return false;
}

bool ARMASMParser::Run(bool, bool NoFinalize) {
  unsigned MainBuffer = SrcMgr.getMainFileID();

  auto InitializePass = [&](unsigned PassNumber) {
    Pass = PassNumber;
    S = PassState();
    S.BuiltinVariables["AREANAME"] = VariableValue::string("");
    S.BuiltinVariables["COMMANDLINE"] =
        VariableValue::string(Opts.CommandLine);
    S.BuiltinVariables["INPUTFILE"] = VariableValue::string("");
    S.BuiltinVariables["LINENUM"] = VariableValue::arithmetic(0);
    S.BuiltinVariables["VAR"] = VariableValue::arithmetic(0);
    S.BuiltinVariables["ARMASM_VERSION"] =
        VariableValue::arithmetic(Traits.getARMASMVersion());
    for (StringRef Predefine : Opts.Predefines)
      if (llvm::Error Err = executePredefine(Predefine)) {
        report(nullptr, SMLoc(), SourceMgr::DK_Error,
               "invalid predefine '" + Predefine +
                   "': " + toString(std::move(Err)));
        return true;
      }
    return false;
  };

  // Pass 1: assembly control and forward definitions.
  if (InitializePass(1) ||
      processBuffer(MainBuffer, InputKind::MainFile, /*NestingDepth=*/0))
    return true;
  FinalRegisterAliases = std::move(S.RegisterAliases);
  // armasm numbers debug files in byte order of their paths.
  for (const auto &File : S.DebugFileSet)
    DebugFileOrder.push_back(File.getKey().str());
  llvm::sort(DebugFileOrder);
  FinalStorageMapFields = std::move(S.StorageMapFields);
  if (HadError)
    return true;

  // Pass 2: emission.
  if (InitializePass(2))
    return true;
  S.SymbolSizes = CollectedSizes;
  getTargetParser().onBeginOfFile();
  beginFile();
  if (processBuffer(MainBuffer, InputKind::MainFile, /*NestingDepth=*/0) ||
      finishFile())
    return true;
  getTargetParser().onEndOfFile();
  printPendingErrors();

  if (!HadError && !NoFinalize) {
    if (MCTargetStreamer *TS = Out.getTargetStreamer())
      TS->emitConstantPools();
    Out.finish(Lexer.getLoc());
  }
  return HadError || Ctx.hadError();
}

} // end anonymous namespace

MCAsmParser *llvm::createMCARMASMParser(SourceMgr &SM, MCContext &C,
                                        MCStreamer &Out, const MCAsmInfo &MAI,
                                        const ARMASMParserOptions &Options) {
  return new ARMASMParser(SM, C, Out, MAI, Options);
}
