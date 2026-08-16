//===-- llvm-armasm64.cpp - ARMASM64-compatible assembler -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/WithColor.h"

#include <optional>

using namespace llvm;
using namespace llvm::opt;

namespace {

enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define OPTTABLE_STR_TABLE_CODE
#include "Opts.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "Opts.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class ARMAsm64OptTable : public GenericOptTable {
public:
  ARMAsm64OptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable,
                        /*IgnoreCase=*/true) {}
};

static void printHelp(StringRef ProgName, const ARMAsm64OptTable &T) {
  std::string Usage = (ProgName + " [options] sourcefile [objectfile]").str();
  T.printHelp(outs(), Usage.c_str(), "LLVM ARMASM64 Assembler");
}

static bool selectDiagnosticOutput(StringRef ProgName, const InputArgList &Args,
                                   std::unique_ptr<raw_fd_ostream> &DiagFile,
                                   raw_ostream *&DiagOS) {
  DiagOS = &outs();
  if (Arg *A = Args.getLastArg(OPT_errors)) {
    std::error_code EC;
    DiagFile =
        std::make_unique<raw_fd_ostream>(A->getValue(), EC, sys::fs::OF_Text);
    if (EC) {
      WithColor::error(*DiagOS, ProgName)
          << A->getValue() << ": " << EC.message() << '\n';
      return false;
    }
    DiagOS = DiagFile.get();
  }
  return true;
}

static void handleDiagnostic(const SMDiagnostic &Diagnostic, void *Context) {
  auto &OS = *static_cast<raw_ostream *>(Context);
  if (Diagnostic.getLoc().isValid())
    Diagnostic.getSourceMgr()->printIncludeStackForDiagnostic(
        Diagnostic.getLoc(), OS);
  Diagnostic.print(nullptr, OS);
}

static Error expandResponseFiles(int Argc, char **Argv,
                                 SmallVectorImpl<const char *> &ExpandedArgv,
                                 StringSaver &Saver) {
  ExpandedArgv.append(Argv, Argv + Argc);
  for (unsigned Depth = 0; Depth != 20; ++Depth) {
    cl::ExpansionContext ECtx(Saver.getAllocator(),
                              cl::TokenizeWindowsCommandLine);
    if (Error Err = ECtx.expandResponseFiles(ExpandedArgv))
      return Err;

    SmallVector<const char *, 20> RewrittenArgv;
    RewrittenArgv.push_back(ExpandedArgv.front());
    bool FoundVia = false;
    for (size_t I = 1; I < ExpandedArgv.size(); ++I) {
      StringRef Arg = ExpandedArgv[I];
      if ((Arg.equals_insensitive("-via") || Arg.equals_insensitive("/via")) &&
          I + 1 < ExpandedArgv.size()) {
        SmallString<128> ResponseFile("@");
        ResponseFile.append(ExpandedArgv[++I]);
        RewrittenArgv.push_back(Saver.save(StringRef(ResponseFile)).data());
        FoundVia = true;
        continue;
      }
      RewrittenArgv.push_back(ExpandedArgv[I]);
    }
    if (!FoundVia)
      return Error::success();
    ExpandedArgv.assign(RewrittenArgv.begin(), RewrittenArgv.end());
  }

  return createStringError(inconvertibleErrorCode(),
                           "response file nesting limit exceeded");
}

static StringRef takeToken(StringRef &Text) {
  Text = Text.ltrim();
  size_t End = Text.find_first_of(" \t");
  StringRef Token = Text.take_front(End);
  Text = End == StringRef::npos ? StringRef() : Text.drop_front(End).ltrim();
  return Token;
}

static StringRef unquoteIdentifier(StringRef Name) {
  Name = Name.trim();
  if (Name.consume_front("|") && Name.consume_back("|"))
    return Name;
  return Name;
}

static StringRef stripComment(StringRef Line) {
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

static ErrorOr<std::unique_ptr<MemoryBuffer>>
openIncludeFile(StringRef Filename, StringRef IncludingFile,
                ArrayRef<std::string> IncludeDirs,
                SmallVectorImpl<char> &ResolvedPath) {
  SmallVector<SmallString<256>, 4> Candidates;
  if (sys::path::is_absolute(Filename)) {
    Candidates.emplace_back(Filename);
  } else {
    SmallString<256> Path(sys::path::parent_path(IncludingFile));
    sys::path::append(Path, Filename);
    Candidates.push_back(Path);
    Candidates.emplace_back(Filename);
    for (StringRef IncludeDir : IncludeDirs) {
      Path = IncludeDir;
      sys::path::append(Path, Filename);
      Candidates.push_back(Path);
    }
  }

  std::error_code EC =
      std::make_error_code(std::errc::no_such_file_or_directory);
  for (const SmallString<256> &Candidate : Candidates) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
        MemoryBuffer::getFile(Candidate, /*IsText=*/true);
    if (Buffer) {
      ResolvedPath.assign(Candidate.begin(), Candidate.end());
      return Buffer;
    }
    EC = Buffer.getError();
  }
  return EC;
}

static void emitLineMarker(raw_ostream &OS, unsigned Line, StringRef Filename) {
  OS << "# " << Line << " \"" << sys::path::convert_to_slash(Filename)
     << "\"\n";
}

static Error expandIncludes(StringRef Remaining, StringRef Filename,
                            ArrayRef<std::string> IncludeDirs, raw_ostream &OS,
                            StringRef ProgName, bool NoWarn,
                            raw_ostream &DiagOS, unsigned Depth = 0) {
  if (Depth == 20)
    return createStringError(Twine(Filename) +
                             ": include nesting limit exceeded");

  bool SawEnd = false;
  unsigned LineNumber = 0;
  while (!Remaining.empty()) {
    ++LineNumber;
    auto [Line, Rest] = Remaining.split('\n');
    Remaining = Rest;
    Line.consume_back("\r");

    StringRef Tail = stripComment(Line).trim();
    StringRef First = takeToken(Tail);
    if (First.equals_insensitive("END")) {
      SawEnd = true;
      break;
    }
    if (!First.equals_insensitive("INCLUDE") &&
        !First.equals_insensitive("GET")) {
      OS << Line << '\n';
      continue;
    }

    StringRef IncludedFilename = takeToken(Tail);
    if (IncludedFilename.empty() || !Tail.empty() ||
        IncludedFilename.front() == '"' || IncludedFilename.front() == '\'')
      return createStringError(Twine(Filename) + ":" + Twine(LineNumber) +
                               ": expected include file name");

    SmallString<256> IncludedPath;
    ErrorOr<std::unique_ptr<MemoryBuffer>> IncludedBuffer =
        openIncludeFile(IncludedFilename, Filename, IncludeDirs, IncludedPath);
    if (!IncludedBuffer)
      return createStringError(Twine(Filename) + ":" + Twine(LineNumber) +
                               ": unable to open include file '" +
                               IncludedFilename +
                               "': " + IncludedBuffer.getError().message());

    emitLineMarker(OS, 1, IncludedPath);
    if (Error Err = expandIncludes((*IncludedBuffer)->getBuffer(), IncludedPath,
                                   IncludeDirs, OS, ProgName, NoWarn, DiagOS,
                                   Depth + 1))
      return Err;
    emitLineMarker(OS, LineNumber + 1, Filename);
  }

  if (!SawEnd && Depth != 0)
    return createStringError(Twine(Filename) +
                             ": unexpected end of file; missing END directive");
  if (!SawEnd && !NoWarn)
    WithColor::warning(DiagOS, ProgName)
        << Filename << ": missing END directive\n";
  return Error::success();
}

static Expected<std::unique_ptr<MemoryBuffer>>
expandInputIncludes(std::unique_ptr<MemoryBuffer> Input,
                    ArrayRef<std::string> IncludeDirs, StringRef ProgName,
                    bool NoWarn, raw_ostream &DiagOS) {
  std::string Expanded;
  raw_string_ostream OS(Expanded);
  if (Error Err =
          expandIncludes(Input->getBuffer(), Input->getBufferIdentifier(),
                         IncludeDirs, OS, ProgName, NoWarn, DiagOS))
    return std::move(Err);
  return MemoryBuffer::getMemBufferCopy(Expanded, Input->getBufferIdentifier());
}

static void printDiagnostic(const SMDiagnostic &Diagnostic,
                            const SourceMgr &SrcMgr, raw_ostream &OS) {
  if (!Diagnostic.getLoc().isValid()) {
    Diagnostic.print(nullptr, OS);
    return;
  }

  SrcMgr.printIncludeStackForDiagnostic(Diagnostic.getLoc(), OS);
  unsigned Buffer = SrcMgr.FindBufferContainingLoc(Diagnostic.getLoc());
  if (!Buffer) {
    Diagnostic.print(nullptr, OS);
    return;
  }

  unsigned DiagnosticLine = SrcMgr.FindLineNumber(Diagnostic.getLoc(), Buffer);
  unsigned PhysicalLine = 0;
  unsigned MarkerPhysicalLine = 0;
  unsigned MarkerSourceLine = 0;
  std::string MarkerFilename;
  StringRef Remaining = SrcMgr.getMemoryBuffer(Buffer)->getBuffer();
  while (!Remaining.empty() && PhysicalLine != DiagnosticLine) {
    ++PhysicalLine;
    auto [Line, Rest] = Remaining.split('\n');
    Remaining = Rest;
    StringRef Tail = Line.trim();
    if (!Tail.consume_front("#"))
      continue;
    StringRef SourceLine = takeToken(Tail);
    unsigned ParsedSourceLine;
    if (SourceLine.getAsInteger(10, ParsedSourceLine))
      continue;
    Tail = Tail.trim();
    if (!Tail.consume_front("\""))
      continue;
    size_t EndQuote = Tail.find('"');
    if (EndQuote == StringRef::npos)
      continue;
    MarkerPhysicalLine = PhysicalLine;
    MarkerSourceLine = ParsedSourceLine;
    MarkerFilename = Tail.take_front(EndQuote).str();
  }

  if (!MarkerPhysicalLine) {
    Diagnostic.print(nullptr, OS);
    return;
  }

  int SourceLine = MarkerSourceLine + DiagnosticLine - MarkerPhysicalLine - 1;
  SMDiagnostic Remapped(SrcMgr, Diagnostic.getLoc(), MarkerFilename, SourceLine,
                        Diagnostic.getColumnNo(), Diagnostic.getKind(),
                        Diagnostic.getMessage(), Diagnostic.getLineContents(),
                        Diagnostic.getRanges(), Diagnostic.getFixIts());
  Remapped.print(nullptr, OS);
}

static void splitOperands(StringRef Text,
                          SmallVectorImpl<StringRef> &Operands) {
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
    if (C == '"' || C == '\'' || C == '|')
      Quote = C;
    else if (C == ',') {
      Operands.push_back(Text.slice(Start, I).trim());
      Start = I + 1;
    }
  }
  Operands.push_back(Text.drop_front(Start).trim());
}

static Expected<std::string> translateString(StringRef String, bool NoEscape) {
  std::string Translated;
  raw_string_ostream OS(Translated);
  OS << '"';
  String = String.drop_front().drop_back();
  for (size_t I = 0; I != String.size(); ++I) {
    char C = String[I];
    if (I + 1 != String.size() && C == '"' && String[I + 1] == '"') {
      OS << "\\\"";
      ++I;
    } else if (I + 1 != String.size() && C == '$' && String[I + 1] == '$') {
      OS << '$';
      ++I;
    } else if (C == '$') {
      return createStringError(inconvertibleErrorCode(),
                               "dollar character in string must be doubled");
    } else if (NoEscape && C == '\\') {
      OS << "\\\\";
    } else {
      OS << C;
    }
  }
  OS << '"';
  return Translated;
}

enum class VariableKind { Arithmetic, Logical, String };

struct VariableValue {
  VariableKind Kind = VariableKind::Arithmetic;
  uint64_t Arithmetic = 0;
  bool Logical = false;
  std::string String;

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
};

using VariableMap = StringMap<VariableValue>;

class VariableExpressionParser {
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
  const VariableMap &Variables;
  const StringMap<uint64_t> &Constants;
  bool NoEscape;
  size_t Position = 0;
  std::string ErrorMessage;

  void skipSpace() {
    while (Position != Text.size() && isSpace(Text[Position]))
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

  bool parseIdentifier(StringRef &Name) {
    skipSpace();
    size_t Start = Position;
    if (consume("|")) {
      size_t End = Text.find('|', Position);
      if (End == StringRef::npos)
        return fail("unterminated quoted variable name");
      Name = Text.slice(Position, End);
      Position = End + 1;
      return true;
    }
    if (Position == Text.size() || !isIdentifierStart(Text[Position]))
      return fail("expected variable or constant");
    while (Position != Text.size() && isIdentifierChar(Text[Position]))
      ++Position;
    Name = Text.slice(Start, Position);
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
    if (consumeInsensitive("{TRUE}")) {
      Result = VariableValue::logical(true);
      return true;
    }
    if (consumeInsensitive("{FALSE}")) {
      Result = VariableValue::logical(false);
      return true;
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

    StringRef Name;
    if (!parseIdentifier(Name))
      return false;
    if (auto It = Variables.find(Name); It != Variables.end()) {
      Result = It->second;
      return true;
    }
    if (auto It = Constants.find(Name); It != Constants.end()) {
      Result = VariableValue::arithmetic(It->second);
      return true;
    }
    return fail("unknown variable or constant '" + Name + "'");
  }

  bool parseUnary(VariableValue &Result) {
    skipSpace();
    if (consume("+")) {
      if (!parseUnary(Result))
        return false;
      return Result.Kind == VariableKind::Arithmetic ||
             fail("unary '+' requires a numeric expression");
    }
    if (consume("-")) {
      if (!parseUnary(Result))
        return false;
      if (Result.Kind != VariableKind::Arithmetic)
        return fail("unary '-' requires a numeric expression");
      Result.Arithmetic = 0U - Result.Arithmetic;
      return true;
    }
    if (consume("~") || consumeInsensitive(":NOT:")) {
      if (!parseUnary(Result))
        return false;
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
      StringRef Name;
      std::string SavedError = std::move(ErrorMessage);
      if (!parseIdentifier(Name))
        return false;
      ErrorMessage = std::move(SavedError);
      Result = VariableValue::logical(Variables.contains(Name) ||
                                      Constants.contains(Name));
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

    struct SymbolOperator {
      StringLiteral Spelling;
      BinaryOperator Operator;
      unsigned Precedence;
    };
    static constexpr SymbolOperator Symbols[] = {
        {">=", BinaryOperator::GreaterEqual, 2},
        {"<=", BinaryOperator::LessEqual, 2},
        {"==", BinaryOperator::Equal, 2},
        {"/=", BinaryOperator::NotEqual, 2},
        {"<>", BinaryOperator::NotEqual, 2},
        {"!=", BinaryOperator::NotEqual, 2},
        {"<<", BinaryOperator::ShiftLeft, 4},
        {">>", BinaryOperator::ShiftRight, 4},
        {"&&", BinaryOperator::LogicalAnd, 1},
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
    for (const SymbolOperator &Entry : Symbols)
      if (consume(Entry.Spelling))
        return ParsedOperator{Entry.Operator, Entry.Precedence};
    return std::nullopt;
  }

  bool requireKinds(const VariableValue &Left, const VariableValue &Right,
                    VariableKind Kind, StringRef Description) {
    return (Left.Kind == Kind && Right.Kind == Kind) ||
           fail(Description + " requires operands of the same type");
  }

  bool applyBinaryOperator(VariableValue &Left, BinaryOperator Operator,
                           const VariableValue &Right) {
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
      Left.Arithmetic /= Right.Arithmetic;
      return true;
    case BinaryOperator::Modulo:
      if (!Numeric())
        return false;
      if (!Right.Arithmetic)
        return fail("division by zero");
      Left.Arithmetic %= Right.Arithmetic;
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
      if (!Numeric())
        return false;
      Left.Arithmetic += Right.Arithmetic;
      return true;
    case BinaryOperator::Subtract:
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
  VariableExpressionParser(StringRef Text, const VariableMap &Variables,
                           const StringMap<uint64_t> &Constants, bool NoEscape)
      : Text(Text), Variables(Variables), Constants(Constants),
        NoEscape(NoEscape) {}

  Expected<VariableValue> parse() {
    VariableValue Result;
    if (!parseExpression(Result, 1))
      return createStringError(inconvertibleErrorCode(), ErrorMessage);
    skipSpace();
    if (Position != Text.size())
      return createStringError(inconvertibleErrorCode(),
                               "unexpected token in expression: " +
                                   Text.drop_front(Position));
    return Result;
  }
};

static std::string quoteStringVariable(StringRef Value, bool NoEscape) {
  std::string Quoted;
  raw_string_ostream OS(Quoted);
  OS << '"';
  for (unsigned char C : Value) {
    switch (C) {
    case '"':
      OS << "\"\"";
      break;
    case '$':
      OS << "$$";
      break;
    case '\\':
      OS << (NoEscape ? "\\" : "\\\\");
      break;
    case '\n':
      OS << "\\n";
      break;
    case '\r':
      OS << "\\r";
      break;
    case '\t':
      OS << "\\t";
      break;
    default:
      if (isPrint(C))
        OS << C;
      else
        OS << '\\' << char('0' + ((C >> 6) & 7)) << char('0' + ((C >> 3) & 7))
           << char('0' + (C & 7));
      break;
    }
  }
  OS << '"';
  return Quoted;
}

static std::string formatSubstitution(const VariableValue &Variable) {
  switch (Variable.Kind) {
  case VariableKind::Arithmetic:
    return utohexstr(Variable.Arithmetic, false, 8);
  case VariableKind::Logical:
    return Variable.Logical ? "T" : "F";
  case VariableKind::String:
    return Variable.String;
  }
  llvm_unreachable("unknown ARMASM variable type");
}

static std::string substituteVariables(StringRef Line,
                                       const VariableMap &Variables) {
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
    auto It = Variables.find(Line.slice(NameStart, NameEnd));
    if (NameEnd == NameStart || It == Variables.end()) {
      OS << C;
      ++I;
      continue;
    }
    OS << formatSubstitution(It->second);
    I = NameEnd;
    if (I != Line.size() && Line[I] == '.')
      ++I;
  }
  return Substituted;
}

static std::string
rewriteVariables(StringRef Text, const VariableMap &Variables, bool NoEscape) {
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  for (size_t I = 0; I != Text.size();) {
    if (Text[I] == '"' || Text[I] == '\'') {
      char Quote = Text[I];
      OS << Text[I++];
      while (I != Text.size()) {
        char C = Text[I++];
        OS << C;
        if (C == '\\' && I != Text.size())
          OS << Text[I++];
        else if (C == Quote) {
          if (Quote == '"' && I != Text.size() && Text[I] == '"')
            OS << Text[I++];
          else
            break;
        }
      }
      continue;
    }
    if (Text[I] == '|') {
      size_t End = Text.find('|', I + 1);
      if (End != StringRef::npos) {
        StringRef Name = Text.slice(I + 1, End);
        auto It = Variables.find(Name);
        if (It != Variables.end()) {
          const VariableValue &Variable = It->second;
          if (Variable.Kind == VariableKind::Arithmetic)
            OS << Variable.Arithmetic;
          else if (Variable.Kind == VariableKind::Logical)
            OS << (Variable.Logical ? "{TRUE}" : "{FALSE}");
          else
            OS << quoteStringVariable(Variable.String, NoEscape);
        } else {
          OS << Text.slice(I, End + 1);
        }
        I = End + 1;
        continue;
      }
    }
    if (!isAlpha(Text[I]) && Text[I] != '_') {
      OS << Text[I++];
      continue;
    }
    size_t End = I + 1;
    while (End != Text.size() && (isAlnum(Text[End]) || Text[End] == '_'))
      ++End;
    StringRef Name = Text.slice(I, End);
    auto It = Variables.find(Name);
    if (It == Variables.end()) {
      OS << Name;
    } else if (It->second.Kind == VariableKind::Arithmetic) {
      OS << It->second.Arithmetic;
    } else if (It->second.Kind == VariableKind::Logical) {
      OS << (It->second.Logical ? "{TRUE}" : "{FALSE}");
    } else {
      OS << quoteStringVariable(It->second.String, NoEscape);
    }
    I = End;
  }
  return Rewritten;
}

static StringRef variableKindName(VariableKind Kind) {
  switch (Kind) {
  case VariableKind::Arithmetic:
    return "arithmetic";
  case VariableKind::Logical:
    return "logical";
  case VariableKind::String:
    return "string";
  }
  llvm_unreachable("unknown ARMASM variable type");
}

static Error declareVariable(StringRef Name, VariableKind Kind,
                             VariableMap &Variables) {
  VariableValue Initial =
      Kind == VariableKind::Arithmetic ? VariableValue::arithmetic(0)
      : Kind == VariableKind::Logical  ? VariableValue::logical(false)
                                       : VariableValue::string("");
  auto It = Variables.find(Name);
  if (It != Variables.end())
    It->second = std::move(Initial);
  else
    Variables[Name] = std::move(Initial);
  return Error::success();
}

static Error assignVariable(StringRef Name, StringRef Directive,
                            StringRef Expression, bool ImplicitDeclaration,
                            VariableMap &Variables,
                            const StringMap<uint64_t> &Constants,
                            bool NoEscape) {
  VariableKind Kind =
      Directive.equals_insensitive("SETA")   ? VariableKind::Arithmetic
      : Directive.equals_insensitive("SETL") ? VariableKind::Logical
                                             : VariableKind::String;
  auto It = Variables.find(Name);
  if (It == Variables.end()) {
    if (!ImplicitDeclaration)
      return createStringError(inconvertibleErrorCode(),
                               "assignment to undeclared variable '" + Name +
                                   "'");
    if (Error Err = declareVariable(Name, Kind, Variables))
      return Err;
    It = Variables.find(Name);
  }
  if (It->second.Kind != Kind)
    return createStringError(
        inconvertibleErrorCode(),
        "cannot assign " + variableKindName(Kind) + " value to " +
            variableKindName(It->second.Kind) + " variable '" + Name + "'");

  VariableExpressionParser Parser(Expression, Variables, Constants, NoEscape);
  Expected<VariableValue> Value = Parser.parse();
  if (!Value)
    return Value.takeError();
  if (Value->Kind != Kind)
    return createStringError(inconvertibleErrorCode(),
                             Directive + " requires a " +
                                 variableKindName(Kind) + " expression");
  It->second = std::move(*Value);
  return Error::success();
}

static void collectExports(StringRef Remaining, StringSet<> &Exports) {
  while (!Remaining.empty()) {
    auto [Line, Rest] = Remaining.split('\n');
    Remaining = Rest;
    Line.consume_back("\r");
    StringRef Tail = stripComment(Line).trim();
    StringRef First = takeToken(Tail);
    if (First.equals_insensitive("EXPORT") ||
        First.equals_insensitive("GLOBAL"))
      Exports.insert(unquoteIdentifier(takeToken(Tail)));
  }
}

static std::string rewriteSymbols(StringRef Text,
                                  const StringMap<std::string> &Symbols) {
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  while (!Text.empty()) {
    size_t Identifier = Text.find_if([](char C) {
      return isAlpha(C) || C == '_' || C == '.' || C == '$' || C == '?';
    });
    if (Identifier == StringRef::npos) {
      OS << Text;
      break;
    }
    OS << Text.take_front(Identifier);
    Text = Text.drop_front(Identifier);
    size_t End = Text.find_if_not([](char C) {
      return isAlnum(C) || C == '_' || C == '.' || C == '$' || C == '?';
    });
    if (End == StringRef::npos)
      End = Text.size();
    StringRef Name = Text.take_front(End);
    auto It = Symbols.find(Name);
    OS << (It == Symbols.end() ? Name : StringRef(It->second));
    Text = Text.drop_front(End);
  }
  return Rewritten;
}

static bool isSetDirective(StringRef Directive) {
  return Directive.equals_insensitive("SETA") ||
         Directive.equals_insensitive("SETL") ||
         Directive.equals_insensitive("SETS");
}

static bool isValidVariableName(StringRef Name) {
  if (Name.size() >= 2 && Name.starts_with("|") && Name.ends_with("|"))
    return Name.size() != 2;
  if (Name.empty() || (!isAlpha(Name.front()) && Name.front() != '_'))
    return false;
  return llvm::all_of(Name.drop_front(),
                      [](char C) { return isAlnum(C) || C == '_'; });
}

static Error executePredefine(StringRef Predefine, VariableMap &Variables,
                              const StringMap<uint64_t> &Constants,
                              bool NoEscape) {
  std::string Substituted = substituteVariables(Predefine, Variables);
  StringRef Tail = stripComment(Substituted).trim();
  StringRef NameToken = takeToken(Tail);
  StringRef Directive = takeToken(Tail);
  if (!isValidVariableName(NameToken) || !isSetDirective(Directive) ||
      Tail.empty())
    return createStringError(
        inconvertibleErrorCode(),
        "expected '<variable> SETA|SETL|SETS <expression>'");
  StringRef Name = unquoteIdentifier(NameToken);
  if (Error Err = assignVariable(Name, Directive, Tail,
                                 /*ImplicitDeclaration=*/true, Variables,
                                 Constants, NoEscape))
    return Err;
  return Error::success();
}

static Expected<std::unique_ptr<MemoryBuffer>>
translateInput(std::unique_ptr<MemoryBuffer> Input, bool NoEscape,
               ArrayRef<std::string> Predefines) {
  StringRef Remaining = Input->getBuffer();
  std::string Translated;
  raw_string_ostream OS(Translated);
  StringMap<std::string> Constants;
  StringMap<uint64_t> AbsoluteConstants;
  VariableMap Variables;
  StringSet<> Exports;
  collectExports(Remaining, Exports);

  for (StringRef Predefine : Predefines)
    if (Error Err =
            executePredefine(Predefine, Variables, AbsoluteConstants, NoEscape))
      return createStringError(inconvertibleErrorCode(),
                               "invalid predefine '" + Predefine +
                                   "': " + toString(std::move(Err)));

  std::string CurrentFilename = Input->getBufferIdentifier().str();
  unsigned CurrentLine = 0;

  while (!Remaining.empty()) {
    auto [Line, Rest] = Remaining.split('\n');
    Remaining = Rest;
    Line.consume_back("\r");
    ++CurrentLine;

    std::string SubstitutedLine = substituteVariables(Line, Variables);
    Line = stripComment(SubstitutedLine);
    StringRef Statement = Line.trim();
    StringRef Tail = Statement;
    StringRef First = takeToken(Tail);
    StringRef AfterFirst = Tail;
    StringRef Second = takeToken(AfterFirst);

    auto SourceError = [&](const Twine &Message) -> Error {
      return createStringError(inconvertibleErrorCode(),
                               Twine(CurrentFilename) + ":" +
                                   Twine(CurrentLine) + ": " + Message);
    };

    if (First.starts_with("#")) {
      StringRef Marker = Statement;
      takeToken(Marker);
      StringRef SourceLine = takeToken(Marker);
      unsigned ParsedLine;
      Marker = Marker.trim();
      if (!SourceLine.getAsInteger(10, ParsedLine) &&
          Marker.consume_front("\"") && Marker.consume_back("\"")) {
        CurrentFilename = Marker.str();
        CurrentLine = ParsedLine - 1;
      }
      OS << Line << '\n';
      continue;
    }

    std::optional<VariableKind> DeclarationKind;
    if (First.equals_insensitive("GBLA"))
      DeclarationKind = VariableKind::Arithmetic;
    else if (First.equals_insensitive("GBLL"))
      DeclarationKind = VariableKind::Logical;
    else if (First.equals_insensitive("GBLS"))
      DeclarationKind = VariableKind::String;
    if (DeclarationKind) {
      StringRef NameToken = takeToken(Tail);
      if (!isValidVariableName(NameToken) || !Tail.empty())
        return SourceError("expected one variable name after " + First);
      StringRef Name = unquoteIdentifier(NameToken);
      if (Constants.contains(Name))
        return SourceError("variable name '" + Name + "' is already defined");
      if (Error Err = declareVariable(Name, *DeclarationKind, Variables))
        return SourceError(toString(std::move(Err)));
      OS << '\n';
      continue;
    }
    if (isSetDirective(Second)) {
      StringRef Name = unquoteIdentifier(First);
      if (!isValidVariableName(First) || AfterFirst.empty())
        return SourceError("expected variable assignment expression");
      if (Error Err = assignVariable(Name, Second, AfterFirst,
                                     /*ImplicitDeclaration=*/false, Variables,
                                     AbsoluteConstants, NoEscape))
        return SourceError(toString(std::move(Err)));
      OS << '\n';
      continue;
    }

    std::string RewrittenLine = rewriteVariables(Line, Variables, NoEscape);
    Line = RewrittenLine;
    Statement = Line.trim();
    Tail = Statement;
    First = takeToken(Tail);
    AfterFirst = Tail;
    Second = takeToken(AfterFirst);

    auto IsDataDirective = [](StringRef Token) {
      return Token.equals_insensitive("DCB") || Token == "=" ||
             Token.equals_insensitive("DCW") ||
             Token.equals_insensitive("DCWU") ||
             Token.equals_insensitive("DCD") ||
             Token.equals_insensitive("DCDU") || Token == "&" ||
             Token.equals_insensitive("DCQ") ||
             Token.equals_insensitive("DCQU");
    };

    if (First.equals_insensitive("EXPORT") ||
        First.equals_insensitive("GLOBAL")) {
      StringRef Name = unquoteIdentifier(takeToken(Tail));
      OS << ".globl " << Name;
    } else if (First.equals_insensitive("IMPORT")) {
      OS << ".globl " << unquoteIdentifier(takeToken(Tail));
    } else if (First.equals_insensitive("EXTERN")) {
      // Unlike IMPORT, an unused EXTERN does not appear in the object.
    } else if (First.equals_insensitive("AREA")) {
      SmallVector<StringRef, 8> Attributes;
      Tail.split(Attributes, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
      StringRef Name = unquoteIdentifier(Attributes.front());
      bool IsCode = false;
      bool IsReadOnly = false;
      bool IsNoInit = false;
      unsigned Alignment = 3;
      for (StringRef Attribute : ArrayRef(Attributes).drop_front()) {
        Attribute = Attribute.trim();
        IsCode |= Attribute.equals_insensitive("CODE");
        IsReadOnly |= Attribute.equals_insensitive("READONLY");
        IsNoInit |= Attribute.equals_insensitive("NOINIT");
        if (Attribute.consume_front_insensitive("ALIGN="))
          (void)Attribute.getAsInteger(0, Alignment);
      }

      StringRef Flags = IsCode       ? "xr"
                        : IsNoInit   ? "bw"
                        : IsReadOnly ? "dr"
                                     : "dw";
      OS << ".section \"" << Name << "\",\"" << Flags << "\""
         << "; .p2align " << Alignment;
    } else if (Second.equals_insensitive("EQU")) {
      StringRef Name = unquoteIdentifier(First);
      std::string TemporaryName = (".Larmasm$" + Name).str();
      Constants[Name] = TemporaryName;
      VariableExpressionParser Parser(AfterFirst, Variables, AbsoluteConstants,
                                      NoEscape);
      Expected<VariableValue> Value = Parser.parse();
      if (Value && Value->Kind == VariableKind::Arithmetic)
        AbsoluteConstants[Name] = Value->Arithmetic;
      else if (!Value)
        consumeError(Value.takeError());
      OS << ".equ " << TemporaryName << ", "
         << rewriteSymbols(AfterFirst, Constants);
    } else if (Second.equals_insensitive("PROC") ||
               Second.equals_insensitive("FUNCTION")) {
      StringRef Name = unquoteIdentifier(First);
      if (!Exports.contains(Name))
        OS << ".def " << Name << "; .scl 6; .endef; ";
      OS << Name << ':';
    } else if (First.equals_insensitive("ENDP") ||
               First.equals_insensitive("ENDFUNC") ||
               Second.equals_insensitive("ENDP") ||
               Second.equals_insensitive("ENDFUNC")) {
    } else if (IsDataDirective(First) || IsDataDirective(Second)) {
      bool HasLabel = !IsDataDirective(First);
      StringRef Directive = HasLabel ? Second : First;
      StringRef Values = HasLabel ? AfterFirst : Tail;
      bool IsByte = Directive.equals_insensitive("DCB") || Directive == "=";
      bool IsWord = Directive.equals_insensitive("DCW") ||
                    Directive.equals_insensitive("DCWU");
      bool IsLong = Directive.equals_insensitive("DCD") ||
                    Directive.equals_insensitive("DCDU") || Directive == "&";
      bool IsUnaligned = Directive.equals_insensitive("DCWU") ||
                         Directive.equals_insensitive("DCDU") ||
                         Directive.equals_insensitive("DCQU");
      unsigned Alignment = IsByte ? 1 : IsWord ? 2 : 4;
      if (!IsUnaligned && Alignment != 1)
        OS << ".balign " << Alignment << "; ";

      if (HasLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (!Exports.contains(Name))
          OS << ".def " << Name << "; .scl 3; .endef; ";
        OS << Name << ":; ";
      }

      if (IsByte) {
        SmallVector<StringRef, 8> Operands;
        splitOperands(Values, Operands);
        for (auto [Index, Operand] : llvm::enumerate(Operands)) {
          if (Index)
            OS << "; ";
          if (Operand.empty()) {
            OS << ".byte (";
          } else if (Operand.size() >= 2 && Operand.front() == '"' &&
                     Operand.back() == '"') {
            Expected<std::string> String = translateString(Operand, NoEscape);
            if (!String)
              return SourceError(toString(String.takeError()));
            OS << ".ascii " << *String;
          } else {
            OS << ".byte " << rewriteSymbols(Operand, Constants);
          }
        }
      } else {
        OS << (IsWord ? ".short " : IsLong ? ".long " : ".quad ");
        if (Values.empty())
          OS << '(';
        else
          OS << rewriteSymbols(Values, Constants);
      }
    } else if (First.equals_insensitive("ALIGN")) {
      OS << ".balign " << Tail;
    } else if (First.equals_insensitive("SPACE")) {
      OS << ".space " << Tail;
    } else if (!First.equals_insensitive("END")) {
      bool IsLabel = !Line.empty() && !isSpace(Line.front()) && Second.empty();
      if (IsLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (!Exports.contains(Name))
          OS << ".def " << Name << "; .scl 6; .endef; ";
        OS << Name << ':';
      } else {
        OS << rewriteSymbols(Line, Constants);
      }
    }
    OS << '\n';
  }

  return MemoryBuffer::getMemBufferCopy(Translated,
                                        Input->getBufferIdentifier());
}

static int assembleInput(StringRef ProgName, StringRef InputFilename,
                         StringRef OutputFilename, StringRef Machine,
                         const InputArgList &Args, raw_ostream &DiagOS) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> InputOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (std::error_code EC = InputOrErr.getError()) {
    WithColor::error(DiagOS, ProgName)
        << InputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  Triple TheTriple(Machine.equals_insensitive("ARM64EC")
                       ? "arm64ec-pc-windows-msvc"
                       : "aarch64-pc-windows-msvc");
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
  if (!TheTarget) {
    WithColor::error(DiagOS, ProgName) << Error << '\n';
    return 1;
  }

  MCTargetOptions MCOptions;
  MCOptions.AssemblyLanguage = "armasm64";
  MCOptions.MCNoWarn = Args.hasArg(OPT_no_warn);

  std::vector<std::string> IncludeDirs;
  for (StringRef Paths : Args.getAllArgValues(OPT_include_path)) {
    SmallVector<StringRef, 4> SplitPaths;
    Paths.split(SplitPaths, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    for (StringRef Path : SplitPaths)
      IncludeDirs.push_back(Path.str());
  }

  Expected<std::unique_ptr<MemoryBuffer>> ExpandedInput =
      expandInputIncludes(std::move(*InputOrErr), IncludeDirs, ProgName,
                          Args.hasArg(OPT_no_warn), DiagOS);
  if (!ExpandedInput) {
    WithColor::error(DiagOS, ProgName)
        << toString(ExpandedInput.takeError()) << '\n';
    return 1;
  }

  std::vector<std::string> Predefines = Args.getAllArgValues(OPT_predefine);
  Expected<std::unique_ptr<MemoryBuffer>> TranslatedInput = translateInput(
      std::move(*ExpandedInput), Args.hasArg(OPT_no_escape), Predefines);
  if (!TranslatedInput) {
    WithColor::error(DiagOS, ProgName)
        << toString(TranslatedInput.takeError()) << '\n';
    return 1;
  }

  SourceMgr SrcMgr;
  SrcMgr.setDiagHandler(handleDiagnostic, &DiagOS);
  SrcMgr.AddNewSourceBuffer(std::move(*TranslatedInput), SMLoc());
  SrcMgr.setIncludeDirs(IncludeDirs);
  SrcMgr.setVirtualFileSystem(vfs::getRealFileSystem());

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TheTriple));
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TheTriple, MCOptions));
  // Microsoft ARMASM64 accepts these extensions without architecture flags.
  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TheTriple, /*CPU=*/"", /*Features=*/"+fullfp16,+dotprod,+sve2"));
  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  if (!MRI || !MAI || !STI || !MCII) {
    WithColor::error(DiagOS, ProgName)
        << "unable to create AArch64 target information\n";
    return 1;
  }

  MCContext Ctx(TheTriple, *MAI, *MRI, *STI, &SrcMgr);
  Ctx.setDiagnosticHandler([&DiagOS](const SMDiagnostic &Diagnostic, bool,
                                     const SourceMgr &SrcMgr,
                                     std::vector<const MDNode *> &) {
    printDiagnostic(Diagnostic, SrcMgr, DiagOS);
  });
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, /*PIC=*/false,
                                        /*LargeCodeModel=*/false));
  Ctx.setObjectFileInfo(MOFI.get());
  Ctx.setMainFileName(InputFilename);
  SmallString<128> CWD;
  if (!sys::fs::current_path(CWD))
    Ctx.setCompilationDir(CWD);

  std::error_code EC;
  auto Out =
      std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::OF_None);
  if (EC) {
    WithColor::error(DiagOS, ProgName)
        << OutputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  std::unique_ptr<buffer_ostream> BOS;
  raw_pwrite_stream *OS = &Out->os();
  if (!Out->os().supportsSeeking()) {
    BOS = std::make_unique<buffer_ostream>(*OS);
    OS = BOS.get();
  }

  std::unique_ptr<MCCodeEmitter> CE(TheTarget->createMCCodeEmitter(*MCII, Ctx));
  std::unique_ptr<MCAsmBackend> MAB(
      TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
  std::unique_ptr<MCObjectWriter> Writer = MAB->createObjectWriter(*OS);
  std::unique_ptr<MCStreamer> Streamer(TheTarget->createMCObjectStreamer(
      TheTriple, Ctx, std::move(MAB), std::move(Writer), std::move(CE), *STI));

  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, Ctx, *Streamer, *MAI));
  std::unique_ptr<MCTargetAsmParser> TargetParser(
      TheTarget->createMCAsmParser(*STI, *Parser, *MCII));
  if (!TargetParser) {
    WithColor::error(DiagOS, ProgName)
        << "AArch64 target does not support assembly parsing\n";
    return 1;
  }
  Parser->setTargetParser(*TargetParser);

  if (Parser->Run(/*NoInitialTextSection=*/true))
    return 1;

  Out->keep();
  return 0;
}

} // namespace

int llvm_armasm64_main(int Argc, char **Argv, const ToolContext &) {
  StringRef ProgName = sys::path::filename(Argv[0]);
  BumpPtrAllocator Alloc;
  StringSaver Saver(Alloc);
  SmallVector<const char *, 20> ExpandedArgv;
  ARMAsm64OptTable T;
  if (Error Err = expandResponseFiles(Argc, Argv, ExpandedArgv, Saver)) {
    unsigned MissingArgIndex, MissingArgCount;
    ArrayRef<const char *> ArgvRef(ExpandedArgv);
    InputArgList Args =
        T.ParseArgs(ArgvRef.drop_front(), MissingArgIndex, MissingArgCount);
    raw_ostream *DiagOS;
    std::unique_ptr<raw_fd_ostream> DiagFile;
    if (!selectDiagnosticOutput(ProgName, Args, DiagFile, DiagOS))
      return 1;
    WithColor::error(*DiagOS, ProgName) << toString(std::move(Err)) << '\n';
    return 1;
  }

  unsigned MissingArgIndex, MissingArgCount;
  ArrayRef<const char *> ArgvRef(ExpandedArgv);
  InputArgList Args =
      T.ParseArgs(ArgvRef.drop_front(), MissingArgIndex, MissingArgCount);

  raw_ostream *DiagOS;
  std::unique_ptr<raw_fd_ostream> DiagFile;
  if (!selectDiagnosticOutput(ProgName, Args, DiagFile, DiagOS))
    return 1;

  if (MissingArgCount) {
    WithColor::error(*DiagOS, ProgName)
        << "missing argument to '" << ArgvRef[MissingArgIndex + 1] << "'\n";
    return 1;
  }

  for (Arg *A : Args.filtered(OPT_UNKNOWN)) {
    StringRef Spelling = A->getSpelling();
    std::string Nearest;
    WithColor::error(*DiagOS, ProgName)
        << "unknown argument '" << Spelling << "'";
    if (T.findNearest(Spelling, Nearest) < 2)
      *DiagOS << "; did you mean '" << Nearest << "'?";
    *DiagOS << '\n';
    return 1;
  }

  if (Args.hasArg(OPT_help)) {
    printHelp(ProgName, T);
    return 0;
  }

  SmallVector<StringRef, 2> Positional;
  for (Arg *A : Args.filtered(OPT_INPUT))
    Positional.push_back(A->getValue());

  if (Positional.empty()) {
    WithColor::error(*DiagOS, ProgName) << "missing input source file\n";
    return 1;
  }
  if (Positional.size() > 2 ||
      (Positional.size() == 2 && Args.hasArg(OPT_output))) {
    WithColor::error(*DiagOS, ProgName) << "too many positional arguments\n";
    return 1;
  }

  StringRef Machine = Args.getLastArgValue(OPT_machine, "ARM64");
  if (!Machine.equals_insensitive("ARM64") &&
      !Machine.equals_insensitive("ARM64EC")) {
    WithColor::error(*DiagOS, ProgName)
        << "invalid machine type '" << Machine << "'\n";
    return 1;
  }

  SmallString<256> DefaultOutput = Positional.front();
  sys::path::replace_extension(DefaultOutput, "obj");
  StringRef Output = Args.getLastArgValue(
      OPT_output,
      Positional.size() == 2 ? Positional.back() : StringRef(DefaultOutput));

  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeAArch64TargetMC();
  LLVMInitializeAArch64AsmParser();
  return assembleInput(ProgName, Positional.front(), Output, Machine, Args,
                       *DiagOS);
}
