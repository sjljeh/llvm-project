//===-- llvm-armasm64.cpp - ARMASM64-compatible assembler -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCAsmParserExtension.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SHA1.h"
#include "llvm/Support/SHA256.h"
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

class ARMAsm64AsmParserExtension final : public MCAsmParserExtension {
  template <bool (ARMAsm64AsmParserExtension::*Handler)(StringRef, SMLoc)>
  void addDirectiveHandler(StringRef Directive) {
    MCAsmParser::ExtensionDirectiveHandler HandlerInfo = std::make_pair(
        this, HandleDirective<ARMAsm64AsmParserExtension, Handler>);
    getParser().addDirectiveHandler(Directive, HandlerInfo);
  }

  bool parseWeakExternal(StringRef, SMLoc) {
    MCSymbol *Symbol;
    MCSymbol *Fallback;
    int64_t Search;
    if (getParser().parseSymbol(Symbol) || getParser().parseComma() ||
        getParser().parseSymbol(Fallback) || getParser().parseComma() ||
        getParser().parseAbsoluteExpression(Search) || parseEOL())
      return true;

    getStreamer().emitWeakReference(Symbol, Fallback);
    static_cast<MCSymbolCOFF *>(Symbol)->setWeakExternalCharacteristics(
        static_cast<COFF::WeakExternalCharacteristics>(Search));
    return false;
  }

  bool parseCommon(StringRef, SMLoc) {
    MCSymbol *Symbol;
    int64_t Size;
    if (getParser().parseSymbol(Symbol) || getParser().parseComma() ||
        getParser().parseAbsoluteExpression(Size) || parseEOL())
      return true;
    getStreamer().emitCommonSymbol(Symbol, Size, Align(1));
    return false;
  }

public:
  void Initialize(MCAsmParser &Parser) override {
    MCAsmParserExtension::Initialize(Parser);
    addDirectiveHandler<&ARMAsm64AsmParserExtension::parseWeakExternal>(
        ".armasm64_weak_external");
    addDirectiveHandler<&ARMAsm64AsmParserExtension::parseCommon>(
        ".armasm64_common");
  }
};

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

static StringRef unquoteIdentifier(StringRef Name) {
  Name = Name.trim();
  if (Name.starts_with("||") && Name.ends_with("||"))
    return Name.drop_front(2).drop_back(2);
  if (Name.starts_with("|") && Name.ends_with("|"))
    return Name.drop_front().drop_back();
  return Name;
}

static std::string getAssemblerSymbolName(StringRef Name) {
  auto IsIdentifierChar = [](char C) {
    return isAlnum(C) || C == '_' || C == '.' || C == '$' || C == '?' ||
           C == '@';
  };
  if (!Name.empty() && llvm::all_of(Name, IsIdentifierChar) &&
      !isDigit(Name.front()))
    return Name.str();

  std::string Quoted;
  raw_string_ostream OS(Quoted);
  OS << '"';
  for (char C : Name) {
    if (C == '"' || C == '\\')
      OS << '\\';
    OS << C;
  }
  OS << '"';
  return Quoted;
}

static std::string quoteAssemblyString(StringRef Value) {
  std::string Quoted;
  raw_string_ostream OS(Quoted);
  OS << '"';
  for (char C : Value) {
    if (C == '"' || C == '\\')
      OS << '\\';
    OS << C;
  }
  OS << '"';
  return Quoted;
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

static void emitListingLineMarker(raw_ostream &OS, unsigned Line) {
  OS << "# armasm64_listing " << Line << '\n';
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

enum class VariableKind { Arithmetic, Logical, String, RegisterRelative };

struct VariableValue {
  VariableKind Kind = VariableKind::Arithmetic;
  uint64_t Arithmetic = 0;
  bool Logical = false;
  std::string String;
  uint64_t RegisterBase = 0;

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
};

using VariableMap = StringMap<VariableValue>;
using RegisterRelativeMap = StringMap<VariableValue>;

static constexpr uint64_t ARMAsmVersion = 145136248;

static std::optional<VariableValue>
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
  if (Name.equals_insensitive("ARMASM_VERSION"))
    return VariableValue::arithmetic(ARMAsmVersion);
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

static bool isBuiltinVariable(StringRef Name,
                              const VariableMap *Context = nullptr) {
  return getBuiltinVariable(Name, Context).has_value();
}

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
  const StringSet<> *DefinedSymbols;
  const RegisterRelativeMap *RegisterRelativeValues;
  const StringMap<uint64_t> *SymbolSizes;
  const VariableMap *BuiltinVariables;
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
      if (Name.equals_insensitive("PC"))
        return fail("{PC} cannot be used in an assembly-time expression");
      if (std::optional<VariableValue> Value =
              getBuiltinVariable(Name, BuiltinVariables)) {
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
    if (RegisterRelativeValues)
      if (auto It = RegisterRelativeValues->find(Name);
          It != RegisterRelativeValues->end()) {
        Result = It->second;
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
    if (consume("!")) {
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
    if (consume("?")) {
      StringRef Name;
      if (!parseIdentifier(Name))
        return false;
      if (!SymbolSizes)
        return fail("unknown symbol '" + Name + "'");
      auto It = SymbolSizes->find(Name);
      if (It == SymbolSizes->end())
        return fail("unknown symbol '" + Name + "'");
      Result = VariableValue::arithmetic(It->second);
      return true;
    }
    if (consumeInsensitive(":RCONST:")) {
      StringRef Name;
      if (!parseIdentifier(Name))
        return false;
      unsigned Register;
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
            isBuiltinVariable(Name, BuiltinVariables));
        return true;
      }
      StringRef Name;
      std::string SavedError = std::move(ErrorMessage);
      if (!parseIdentifier(Name))
        return false;
      ErrorMessage = std::move(SavedError);
      Result = VariableValue::logical(
          Variables.contains(Name) || Constants.contains(Name) ||
          (DefinedSymbols && DefinedSymbols->contains(Name)));
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
  VariableExpressionParser(
      StringRef Text, const VariableMap &Variables,
      const StringMap<uint64_t> &Constants, bool NoEscape,
      const StringSet<> *DefinedSymbols = nullptr,
      const RegisterRelativeMap *RegisterRelativeValues = nullptr,
      const StringMap<uint64_t> *SymbolSizes = nullptr,
      const VariableMap *BuiltinVariables = nullptr)
      : Text(Text), Variables(Variables), Constants(Constants),
        DefinedSymbols(DefinedSymbols),
        RegisterRelativeValues(RegisterRelativeValues),
        SymbolSizes(SymbolSizes), BuiltinVariables(BuiltinVariables),
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
  case VariableKind::RegisterRelative:
    llvm_unreachable("register-relative value cannot be substituted");
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

static std::string rewriteVariables(StringRef Text,
                                    const VariableMap &Variables, bool NoEscape,
                                    StringRef PCSymbol = {},
                                    bool *UsesPC = nullptr,
                                    const VariableMap *BuiltinVariables = nullptr) {
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  auto EmitVariable = [&](const VariableValue &Variable) {
    if (Variable.Kind == VariableKind::Arithmetic)
      OS << Variable.Arithmetic;
    else if (Variable.Kind == VariableKind::Logical)
      OS << (Variable.Logical ? "{TRUE}" : "{FALSE}");
    else
      OS << quoteStringVariable(Variable.String, NoEscape);
  };
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
    if (Text[I] == '{') {
      size_t End = Text.find('}', I + 1);
      if (End != StringRef::npos) {
        StringRef Name = Text.slice(I + 1, End);
        if (Name.equals_insensitive("PC")) {
          if (PCSymbol.empty())
            OS << Text.slice(I, End + 1);
          else {
            OS << PCSymbol;
            if (UsesPC)
              *UsesPC = true;
          }
          I = End + 1;
          continue;
        }
        if (std::optional<VariableValue> Variable =
                getBuiltinVariable(Name, BuiltinVariables)) {
          EmitVariable(*Variable);
          I = End + 1;
          continue;
        }
      }
    }
    if (Text[I] == '|') {
      size_t End = Text.find('|', I + 1);
      if (End != StringRef::npos) {
        StringRef Name = Text.slice(I + 1, End);
        auto It = Variables.find(Name);
        if (It != Variables.end())
          EmitVariable(It->second);
        else
          OS << Text.slice(I, End + 1);
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
    if (It == Variables.end())
      OS << Name;
    else
      EmitVariable(It->second);
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
  case VariableKind::RegisterRelative:
    return "register-relative";
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
                             const StringMap<uint64_t> &Constants, bool NoEscape,
                             const VariableMap *ExpressionVariables = nullptr,
                             const StringSet<> *DefinedSymbols = nullptr,
                             const VariableMap *BuiltinVariables = nullptr) {
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

  VariableExpressionParser Parser(
      Expression, ExpressionVariables ? *ExpressionVariables : Variables,
      Constants, NoEscape, DefinedSymbols,
      /*RegisterRelativeValues=*/nullptr, /*SymbolSizes=*/nullptr,
      BuiltinVariables);
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

enum class ExportType { None, Data, Function };

struct ExportSpec {
  std::string Name;
  ExportType Type = ExportType::None;
};

static Expected<ExportSpec> parseExportSpec(StringRef Text) {
  Text = Text.trim();
  if (Text.empty())
    return createStringError(inconvertibleErrorCode(),
                             "A2003: improper line syntax");

  StringRef Name = Text;
  ExportType Type = ExportType::None;
  if (size_t Open = Text.find('['); Open != StringRef::npos) {
    size_t Close = Text.rfind(']');
    if (Close == StringRef::npos || !Text.drop_front(Close + 1).trim().empty())
      return createStringError(inconvertibleErrorCode(),
                               "A2075: improper line syntax; expected: ']'");
    Name = Text.take_front(Open).trim();
    StringRef Attribute = Text.slice(Open + 1, Close).trim();
    if (Attribute.equals_insensitive("DATA"))
      Type = ExportType::Data;
    else if (Attribute.equals_insensitive("FUNC"))
      Type = ExportType::Function;
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

using ExportMap = StringMap<ExportType>;

static void collectExports(StringRef Remaining, ExportMap &Exports) {
  while (!Remaining.empty()) {
    auto [Line, Rest] = Remaining.split('\n');
    Remaining = Rest;
    Line.consume_back("\r");
    StringRef Tail = stripComment(Line).trim();
    StringRef First = takeToken(Tail);
    if (First.equals_insensitive("EXPORT") ||
        First.equals_insensitive("GLOBAL")) {
      Expected<ExportSpec> Spec = parseExportSpec(Tail);
      if (Spec)
        Exports[Spec->Name] = Spec->Type;
      else
        consumeError(Spec.takeError());
    }
  }
}

static std::string rewriteSymbols(StringRef Text,
                                  const StringMap<std::string> &Symbols) {
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  while (!Text.empty()) {
    size_t Bar = Text.find('|');
    size_t Identifier = Text.find_if([](char C) {
      return isAlpha(C) || C == '_' || C == '.' || C == '$' || C == '?';
    });
    if (Bar != StringRef::npos &&
        (Identifier == StringRef::npos || Bar < Identifier)) {
      bool DoubleBars = Text.drop_front(Bar).starts_with("||");
      size_t NameStart = Bar + (DoubleBars ? 2 : 1);
      size_t End = DoubleBars ? Text.find("||", NameStart)
                              : Text.find('|', NameStart);
      if (NameStart < Text.size() && !isSpace(Text[NameStart]) &&
          End != StringRef::npos) {
        OS << Text.take_front(Bar);
        StringRef Name = Text.slice(NameStart, End);
        auto It = Symbols.find(Name);
        OS << (It == Symbols.end() ? getAssemblerSymbolName(Name)
                                   : It->second);
        Text = Text.drop_front(End + (DoubleBars ? 2 : 1));
        continue;
      }
    }
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

static std::string
normalizeSymbolicExpression(StringRef Text,
                            const StringMap<std::string> &Symbols) {
  std::string RewrittenSymbols = rewriteSymbols(Text, Symbols);
  Text = RewrittenSymbols;
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  char PreviousNonSpace = '\0';
  while (!Text.empty()) {
    struct OperatorSpelling {
      StringLiteral ARMAsm;
      StringLiteral GNU;
    };
    static constexpr OperatorSpelling Operators[] = {
        {":SHL:", "<<"}, {":SHR:", ">>"}, {":MOD:", "%"}, {":AND:", "&"},
        {":EOR:", "^"},  {":OR:", "|"},   {":NOT:", "~"},
    };
    bool ReplacedOperator = false;
    for (const OperatorSpelling &Operator : Operators) {
      if (Text.starts_with_insensitive(Operator.ARMAsm)) {
        OS << Operator.GNU;
        PreviousNonSpace = Operator.GNU.back();
        Text = Text.drop_front(Operator.ARMAsm.size());
        ReplacedOperator = true;
        break;
      }
    }
    if (ReplacedOperator)
      continue;

    auto CanStartPrimary = [PreviousNonSpace]() {
      return PreviousNonSpace == '\0' ||
             StringRef("([,+-*/%&|^~<=>").contains(PreviousNonSpace);
    };
    if (Text.front() == '|' && Text.drop_front().contains('|') &&
        CanStartPrimary()) {
      size_t End = Text.find('|', 1);
      OS << getAssemblerSymbolName(Text.slice(1, End));
      PreviousNonSpace = '"';
      Text = Text.drop_front(End + 1);
      continue;
    }
    if (Text.front() == '&' && Text.size() > 1 && isHexDigit(Text[1]) &&
        CanStartPrimary()) {
      OS << "0x";
      PreviousNonSpace = 'x';
      Text = Text.drop_front();
      continue;
    }
    if (Text.size() > 2 && Text.front() >= '2' && Text.front() <= '9' &&
        Text[1] == '_' && CanStartPrimary()) {
      unsigned Radix = Text.front() - '0';
      size_t End = 2;
      while (End != Text.size() && isAlnum(Text[End]))
        ++End;
      uint64_t Value;
      if (!Text.slice(2, End).getAsInteger(Radix, Value)) {
        OS << Value;
        PreviousNonSpace = '0';
        Text = Text.drop_front(End);
        continue;
      }
    }
    char C = Text.front();
    OS << C;
    if (!isSpace(C))
      PreviousNonSpace = C;
    Text = Text.drop_front();
  }
  return Rewritten;
}

static bool isLogicalImmediate(uint64_t Value, unsigned Width) {
  if (Width == 32)
    Value &= UINT32_MAX;
  for (unsigned ElementSize = 2; ElementSize <= Width; ElementSize *= 2) {
    uint64_t Mask = ElementSize == 64 ? UINT64_MAX : (1ULL << ElementSize) - 1;
    uint64_t Pattern = Value & Mask;
    uint64_t Replicated = 0;
    for (unsigned Offset = 0; Offset != Width; Offset += ElementSize)
      Replicated |= Pattern << Offset;
    if (Replicated != Value || Pattern == 0 || Pattern == Mask)
      continue;
    for (unsigned Rotate = 0; Rotate != ElementSize; ++Rotate) {
      uint64_t Rotated =
          Rotate == 0
              ? Pattern
              : ((Pattern >> Rotate) | (Pattern << (ElementSize - Rotate))) &
                    Mask;
      if ((Rotated & (Rotated + 1)) == 0)
        return true;
    }
  }
  return false;
}

static bool isSingleMovImmediate(uint64_t Value, unsigned Width) {
  if (Width == 32)
    Value &= UINT32_MAX;
  unsigned NonZeroChunks = 0;
  unsigned NonOneChunks = 0;
  for (unsigned Shift = 0; Shift != Width; Shift += 16) {
    uint64_t Chunk = (Value >> Shift) & UINT16_MAX;
    NonZeroChunks += Chunk != 0;
    NonOneChunks += Chunk != UINT16_MAX;
  }
  return NonZeroChunks <= 1 || NonOneChunks <= 1 ||
         isLogicalImmediate(Value, Width);
}

static bool isIntegerValueInRange(uint64_t Value, int64_t Minimum,
                                  uint64_t Maximum) {
  int64_t SignedValue = static_cast<int64_t>(Value);
  return SignedValue < 0 ? SignedValue >= Minimum : Value <= Maximum;
}

static bool containsIdentifier(StringRef Text, StringRef Name) {
  auto IsIdentifierChar = [](char C) {
    return isAlnum(C) || C == '_' || C == '.' || C == '$' || C == '?';
  };
  size_t Offset = 0;
  while ((Offset = Text.find(Name, Offset)) != StringRef::npos) {
    bool HasStartBoundary = Offset == 0 || !IsIdentifierChar(Text[Offset - 1]);
    size_t End = Offset + Name.size();
    bool HasEndBoundary = End == Text.size() || !IsIdentifierChar(Text[End]);
    if (HasStartBoundary && HasEndBoundary)
      return true;
    ++Offset;
  }
  return false;
}

enum class RegisterAliasKind { Single, Double, Quad };

struct RegisterAlias {
  RegisterAliasKind Kind = RegisterAliasKind::Single;
  unsigned Number = 0;
};

using RegisterAliasMap = StringMap<RegisterAlias>;

struct StorageMapField {
  int64_t Offset = 0;
  std::optional<std::string> BaseRegister;
};

using StorageMapFieldMap = StringMap<StorageMapField>;

static std::optional<unsigned> getStorageMapBaseEncoding(StringRef Register) {
  Register = Register.trim();
  if (Register.equals_insensitive("fp"))
    return 63;
  if (Register.equals_insensitive("lr"))
    return 64;
  if (Register.equals_insensitive("sp"))
    return 65;
  if (!Register.consume_front_insensitive("x"))
    return std::nullopt;
  unsigned Number;
  if (Register.getAsInteger(10, Number) || Number > 30)
    return std::nullopt;
  return 34 + Number;
}

static std::optional<RegisterAliasKind>
registerAliasKindForDirective(StringRef Directive) {
  if (Directive.equals_insensitive("SN"))
    return RegisterAliasKind::Single;
  if (Directive.equals_insensitive("DN"))
    return RegisterAliasKind::Double;
  if (Directive.equals_insensitive("QN"))
    return RegisterAliasKind::Quad;
  return std::nullopt;
}

static std::optional<std::pair<RegisterAliasKind, unsigned>>
parseExtensionRegisterName(StringRef Name) {
  if (Name.size() < 2)
    return std::nullopt;
  RegisterAliasKind Kind;
  switch (toLower(Name.front())) {
  case 's':
    Kind = RegisterAliasKind::Single;
    break;
  case 'd':
    Kind = RegisterAliasKind::Double;
    break;
  case 'q':
    Kind = RegisterAliasKind::Quad;
    break;
  default:
    return std::nullopt;
  }
  unsigned Number;
  if (Name.drop_front().getAsInteger(10, Number) || Number > 31)
    return std::nullopt;
  return std::make_pair(Kind, Number);
}

static bool isPredefinedRegisterName(StringRef Name) {
  if (parseExtensionRegisterName(Name))
    return true;
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

static Expected<unsigned>
parseRegisterAliasTarget(StringRef Expression, RegisterAliasKind Kind,
                         const RegisterAliasMap &Aliases, bool NoEscape) {
  Expression = Expression.trim();
  if (auto It = Aliases.find(unquoteIdentifier(Expression));
      It != Aliases.end()) {
    if (It->second.Kind != Kind)
      return createStringError(inconvertibleErrorCode(),
                               "A2046: unknown or bad symbol type: " +
                                   Expression);
    return It->second.Number;
  }

  if (std::optional<std::pair<RegisterAliasKind, unsigned>> Register =
          parseExtensionRegisterName(Expression)) {
    if (Register->first != Kind)
      return createStringError(inconvertibleErrorCode(),
                               "A2046: unknown or bad symbol type: " +
                                   Expression);
    return Register->second;
  }

  VariableMap EmptyVariables;
  StringMap<uint64_t> EmptyConstants;
  VariableExpressionParser Parser(Expression, EmptyVariables, EmptyConstants,
                                  NoEscape);
  Expected<VariableValue> Value = Parser.parse();
  if (!Value || Value->Kind != VariableKind::Arithmetic) {
    if (!Value)
      consumeError(Value.takeError());
    return createStringError(inconvertibleErrorCode(),
                             Expression.contains('.')
                                 ? "A2003: improper line syntax"
                                 : "A2046: unknown or bad symbol type: " +
                                       Expression);
  }
  if (Value->Arithmetic > 31)
    return createStringError(
        inconvertibleErrorCode(),
        "A2069: immediate value " +
            Twine(static_cast<int64_t>(Value->Arithmetic)) +
            " out of range; expected values: [0,1,..31]");

  return Value->Arithmetic;
}

static std::string rewriteRegisterAliases(StringRef Text,
                                          const RegisterAliasMap &Aliases) {
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  auto EmitAlias = [&](const RegisterAlias &Alias, StringRef Remaining) {
    bool IsVectorUse =
        Alias.Kind != RegisterAliasKind::Single &&
        (Remaining.starts_with(".") || Remaining.starts_with("["));
    char Prefix = IsVectorUse                               ? 'v'
                  : Alias.Kind == RegisterAliasKind::Single ? 's'
                  : Alias.Kind == RegisterAliasKind::Double ? 'd'
                                                            : 'q';
    OS << Prefix << Alias.Number;
  };
  while (!Text.empty()) {
    size_t Identifier = Text.find_if(
        [](char C) { return isAlpha(C) || C == '_' || C == '$' || C == '?'; });
    size_t Bar = Text.find('|');
    if (Bar != StringRef::npos &&
        (Identifier == StringRef::npos || Bar < Identifier)) {
      size_t End = Text.find('|', Bar + 1);
      if (End != StringRef::npos) {
        StringRef Name = Text.slice(Bar + 1, End);
        if (auto It = Aliases.find(Name); It != Aliases.end()) {
          OS << Text.take_front(Bar);
          EmitAlias(It->second, Text.drop_front(End + 1));
          Text = Text.drop_front(End + 1);
          continue;
        }
      }
    }
    if (Identifier == StringRef::npos) {
      OS << Text;
      break;
    }
    OS << Text.take_front(Identifier);
    Text = Text.drop_front(Identifier);
    size_t End = Text.find_if_not(
        [](char C) { return isAlnum(C) || C == '_' || C == '$' || C == '?'; });
    if (End == StringRef::npos)
      End = Text.size();
    StringRef Name = Text.take_front(End);
    auto It = Aliases.find(Name);
    if (It == Aliases.end()) {
      OS << Name;
    } else {
      EmitAlias(It->second, Text.drop_front(End));
    }
    Text = Text.drop_front(End);
  }
  return Rewritten;
}

static std::string rewriteStorageMapFields(StringRef Text,
                                           const StorageMapFieldMap &Fields) {
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  auto EmitField = [&](const StorageMapField &Field) {
    if (Field.BaseRegister)
      OS << '[' << *Field.BaseRegister << ", #" << Field.Offset << ']';
    else
      OS << Field.Offset;
  };
  while (!Text.empty()) {
    size_t Identifier = Text.find_if(
        [](char C) { return isAlpha(C) || C == '_' || C == '$' || C == '?'; });
    size_t Bar = Text.find('|');
    if (Bar != StringRef::npos &&
        (Identifier == StringRef::npos || Bar < Identifier)) {
      size_t End = Text.find('|', Bar + 1);
      if (End != StringRef::npos) {
        StringRef Name = Text.slice(Bar + 1, End);
        if (auto It = Fields.find(Name); It != Fields.end()) {
          OS << Text.take_front(Bar);
          EmitField(It->second);
          Text = Text.drop_front(End + 1);
          continue;
        }
      }
    }
    if (Identifier == StringRef::npos) {
      OS << Text;
      break;
    }
    OS << Text.take_front(Identifier);
    Text = Text.drop_front(Identifier);
    size_t End = Text.find_if_not(
        [](char C) { return isAlnum(C) || C == '_' || C == '$' || C == '?'; });
    if (End == StringRef::npos)
      End = Text.size();
    StringRef Name = Text.take_front(End);
    auto It = Fields.find(Name);
    if (It == Fields.end())
      OS << Name;
    else
      EmitField(It->second);
    Text = Text.drop_front(End);
  }
  return Rewritten;
}

struct NumericLocalLabel {
  unsigned Number;
  StringRef RoutName;
};

static std::optional<NumericLocalLabel>
parseNumericLocalLabel(StringRef Token) {
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

static Expected<std::string>
rewriteNumericLocalLabelReferences(StringRef Text, unsigned Scope,
                                   StringRef RoutName,
                                   const DenseSet<unsigned> &DefinedLabels) {
  std::string Rewritten;
  raw_string_ostream OS(Rewritten);
  char Quote = '\0';
  for (size_t I = 0; I != Text.size();) {
    char C = Text[I];
    if (Quote) {
      OS << C;
      ++I;
      if (C == Quote)
        Quote = '\0';
      continue;
    }
    if (C == '"' || C == '\'' || C == '|') {
      Quote = C;
      OS << C;
      ++I;
      continue;
    }
    if (C != '%' || (I != 0 && (isAlnum(Text[I - 1]) || Text[I - 1] == ')'))) {
      OS << C;
      ++I;
      continue;
    }

    size_t End = I + 1;
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
    if (NumberStart == End) {
      OS << C;
      ++I;
      continue;
    }
    unsigned Number;
    if (Text.slice(NumberStart, End).getAsInteger(10, Number) || Number > 99)
      return createStringError(inconvertibleErrorCode(),
                               "numeric local label must be in range 0-99");
    size_t NameStart = End;
    while (End != Text.size() && (isAlnum(Text[End]) || Text[End] == '_'))
      ++End;
    StringRef ReferenceRoutName = Text.slice(NameStart, End);
    if (!ReferenceRoutName.empty() && ReferenceRoutName != RoutName)
      return createStringError(inconvertibleErrorCode(),
                               "numeric local label ROUT name mismatch");
    if (!Direction)
      Direction = DefinedLabels.contains(Number) ? 'b' : 'f';
    OS << Scope * 100 + Number << Direction;
    I = End;
  }
  return Rewritten;
}

static bool isSetDirective(StringRef Directive) {
  return Directive.equals_insensitive("SETA") ||
         Directive.equals_insensitive("SETL") ||
         Directive.equals_insensitive("SETS");
}

static bool isEquDirective(StringRef Directive) {
  return Directive.equals_insensitive("EQU") || Directive == "*";
}

static bool isDataDirective(StringRef Token) {
  return Token.equals_insensitive("DCB") || Token == "=" ||
         Token.equals_insensitive("DCW") || Token.equals_insensitive("DCWU") ||
         Token.equals_insensitive("DCD") || Token.equals_insensitive("DCDU") ||
         Token == "&" || Token.equals_insensitive("DCQ") ||
         Token.equals_insensitive("DCQU") || Token.equals_insensitive("DCFS") ||
         Token.equals_insensitive("DCFSU") ||
         Token.equals_insensitive("DCFD") ||
         Token.equals_insensitive("DCFDU") || Token.equals_insensitive("DCI") ||
         Token.equals_insensitive("DCI.W");
}

static bool isStorageDirective(StringRef Token) {
  return Token.equals_insensitive("SPACE") ||
         Token.equals_insensitive("FILL") || Token == "%";
}

struct AreaName {
  StringRef Name;
  StringRef ComdatSymbol;
};

static Expected<AreaName> parseAreaName(StringRef Text) {
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

static StringRef getARM64RelocationName(uint64_t Type) {
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

static void recordDefinedSymbols(StringRef Line, StringRef First,
                                 StringRef Second, StringRef Tail,
                                 StringSet<> &DefinedSymbols) {
  if (First.equals_insensitive("IMPORT") ||
      First.equals_insensitive("EXTERN")) {
    StringRef Symbol = takeToken(Tail).split(',').first;
    if (!Symbol.empty())
      DefinedSymbols.insert(unquoteIdentifier(Symbol));
  }
  if (First.equals_insensitive("ALIAS")) {
    SmallVector<StringRef, 2> Operands;
    splitOperands(Tail, Operands);
    if (Operands.size() == 2)
      DefinedSymbols.insert(unquoteIdentifier(Operands[1]));
  }
  if (First.equals_insensitive("COMMON")) {
    StringRef Symbol = takeToken(Tail).split(',').first;
    if (!Symbol.empty())
      DefinedSymbols.insert(unquoteIdentifier(Symbol));
  }
  if (!Line.empty() && !isSpace(Line.front()) &&
      (Second.empty() || isEquDirective(Second) ||
       Second.equals_insensitive("PROC") ||
       Second.equals_insensitive("FUNCTION") ||
       Second.equals_insensitive("FIELD") ||
       Second.equals_insensitive("ROUT") || isDataDirective(Second) ||
       isStorageDirective(Second)))
    DefinedSymbols.insert(unquoteIdentifier(First));
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

struct AssemblySourceLine {
  std::string Text;
  std::string Filename;
  unsigned Line;
};

struct MacroDefinition {
  struct Parameter {
    std::string Name;
    std::optional<std::string> DefaultValue;
  };

  std::optional<std::string> LabelParameter;
  SmallVector<Parameter, 4> Parameters;
  SmallVector<AssemblySourceLine, 8> Body;
};

static std::string
substituteMacroParameters(StringRef Line,
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

static std::string normalizeMacroArgument(StringRef Argument) {
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

class AssemblyControlExpander {
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

  ArrayRef<std::string> IncludeDirs;
  StringRef ProgName;
  bool NoWarn;
  bool NoEscape;
  const DenseSet<unsigned> &IgnoredWarnings;
  raw_ostream &DiagOS;
  raw_ostream &OS;
  VariableMap &Variables;
  StringMap<uint64_t> &Constants;
  StringMap<MacroDefinition> Macros;
  StringSet<> DefinedSymbols;
  VariableMap BuiltinVariables;
  uint64_t StorageMapOffset = 0;
  unsigned ListingLine = 0;
  VariableMap *LocalVariables = nullptr;
  const StringMap<std::string> *MacroParameters = nullptr;

  static Error sourceError(StringRef Filename, unsigned Line,
                           const Twine &Message) {
    return createStringError(inconvertibleErrorCode(), Twine(Filename) + ":" +
                                                           Twine(Line) + ": " +
                                                           Message);
  }

  static Error sourceError(const AssemblySourceLine &Line,
                           const Twine &Message) {
    return sourceError(Line.Filename, Line.Line, Message);
  }

  VariableMap effectiveVariables() const {
    VariableMap Effective = Variables;
    if (LocalVariables)
      for (const auto &Variable : *LocalVariables)
        Effective[Variable.getKey()] = Variable.getValue();
    return Effective;
  }

  std::string prepareLine(const AssemblySourceLine &Line) const {
    std::string Expanded =
        MacroParameters ? substituteMacroParameters(Line.Text, *MacroParameters)
                        : Line.Text;
    VariableMap Effective = effectiveVariables();
    return substituteVariables(Expanded, Effective);
  }

  Expected<VariableValue> evaluateExpression(StringRef Expression) {
    VariableMap Effective = effectiveVariables();
    VariableExpressionParser Parser(Expression, Effective, Constants, NoEscape,
                                    &DefinedSymbols,
                                    /*RegisterRelativeValues=*/nullptr,
                                    /*SymbolSizes=*/nullptr, &BuiltinVariables);
    return Parser.parse();
  }

  Expected<bool> evaluateCondition(StringRef Expression) {
    Expected<VariableValue> Value = evaluateExpression(Expression);
    if (!Value)
      return Value.takeError();
    if (Value->Kind == VariableKind::Logical)
      return Value->Logical;
    if (Value->Kind == VariableKind::Arithmetic)
      return Value->Arithmetic != 0;
    return createStringError(inconvertibleErrorCode(),
                             "condition requires a numeric or logical "
                             "expression");
  }

  void reportWarning(const AssemblySourceLine &Line, unsigned Code,
                     StringRef Message) {
    if (NoWarn || IgnoredWarnings.contains(Code))
      return;
    WithColor::warning(DiagOS, ProgName)
        << Line.Filename << ":" << Line.Line << ": A" << Code << ": " << Message
        << '\n';
  }

  Expected<bool> evaluateConditionLine(const AssemblySourceLine &Line) {
    std::string Substituted = prepareLine(Line);
    StringRef Tail = stripComment(Substituted).trim();
    takeToken(Tail);
    return evaluateCondition(Tail);
  }

  Error defineMacro(ArrayRef<AssemblySourceLine> Lines, size_t &LineIndex,
                    bool Active, unsigned NestingDepth) {
    const AssemblySourceLine &MacroLine = Lines[LineIndex];
    if (LineIndex + 1 == Lines.size())
      return sourceError(MacroLine,
                         "missing macro prototype and MEND directive");

    size_t PrototypeIndex = LineIndex + 1;
    size_t EndIndex = PrototypeIndex + 1;
    unsigned DefinitionDepth = 1;
    for (; EndIndex != Lines.size(); ++EndIndex) {
      std::string Expanded = prepareLine(Lines[EndIndex]);
      StringRef Tail = stripComment(Expanded).trim();
      StringRef First = takeToken(Tail);
      if (First.equals_insensitive("MACRO")) {
        if (NestingDepth + DefinitionDepth == 256)
          return sourceError(Lines[EndIndex],
                             "assembly control nesting limit exceeded");
        ++DefinitionDepth;
      } else if (First.equals_insensitive("MEND") && --DefinitionDepth == 0) {
        break;
      }
    }
    if (EndIndex == Lines.size())
      return sourceError(Lines.back().Filename, Lines.back().Line + 1,
                         "unexpected end of file; missing MEND directive");

    LineIndex = EndIndex + 1;
    if (!Active)
      return Error::success();

    std::string ExpandedPrototype = prepareLine(Lines[PrototypeIndex]);
    StringRef Tail = stripComment(ExpandedPrototype).trim();
    StringRef NameToken = takeToken(Tail);
    std::optional<std::string> LabelParameter;
    if (NameToken.consume_front("$")) {
      if (!isValidVariableName(NameToken))
        return sourceError(Lines[PrototypeIndex],
                           "invalid macro label parameter");
      LabelParameter = NameToken.str();
      NameToken = takeToken(Tail);
    }
    if (!isValidVariableName(NameToken))
      return sourceError(Lines[PrototypeIndex], "invalid macro name");

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
          return sourceError(Lines[PrototypeIndex],
                             "macro parameters must begin with '$'");
        size_t Equal = Parameter.find('=');
        StringRef Name = Parameter.take_front(Equal).trim();
        if (!isValidVariableName(Name))
          return sourceError(Lines[PrototypeIndex],
                             "invalid macro parameter name");
        if (!ParameterNames.insert(Name).second)
          return sourceError(Lines[PrototypeIndex],
                             "duplicate macro parameter '" + Name + "'");
        std::optional<std::string> DefaultValue;
        if (Equal != StringRef::npos)
          DefaultValue =
              normalizeMacroArgument(Parameter.drop_front(Equal + 1));
        Definition.Parameters.push_back({Name.str(), std::move(DefaultValue)});
      }
    }
    Definition.Body.append(Lines.begin() + PrototypeIndex + 1,
                           Lines.begin() + EndIndex);

    if (!Macros.try_emplace(NameToken, std::move(Definition)).second)
      return sourceError(Lines[EndIndex],
                         "macro name '" + NameToken + "' is already defined");
    return Error::success();
  }

  Error expandMacro(const MacroDefinition &Definition, StringRef Name,
                    bool HasLabel, StringRef Label, StringRef ArgumentText,
                    const AssemblySourceLine &Invocation,
                    unsigned NestingDepth) {
    SmallVector<StringRef, 8> Arguments;
    if (!ArgumentText.trim().empty())
      splitOperands(ArgumentText, Arguments);
    if (Arguments.size() > Definition.Parameters.size())
      return sourceError(Invocation,
                         "too many arguments to macro '" + Name + "'");

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
    else if (HasLabel) {
      emitListingLineMarker(OS, ListingLine);
      emitLineMarker(OS, Invocation.Line, Invocation.Filename);
      OS << Label << '\n';
    }

    VariableMap Locals;
    VariableMap *SavedLocals = LocalVariables;
    const StringMap<std::string> *SavedParameters = MacroParameters;
    LocalVariables = &Locals;
    MacroParameters = &Bindings;
    bool DidExit = false;
    Error Err = processLines(Definition.Body, InputKind::Macro, NestingDepth,
                             DidExit, Invocation.Filename);
    MacroParameters = SavedParameters;
    LocalVariables = SavedLocals;
    return Err;
  }

  Error assignCurrentVariable(StringRef Name, StringRef Directive,
                              StringRef Expression) {
    bool IsLocal = LocalVariables && LocalVariables->contains(Name);
    VariableMap &Target = IsLocal ? *LocalVariables : Variables;
    VariableMap Effective = effectiveVariables();
    return assignVariable(Name, Directive, Expression,
                          /*ImplicitDeclaration=*/false, Target, Constants,
                          NoEscape, &Effective, &DefinedSymbols,
                          &BuiltinVariables);
  }

  Error processFile(std::unique_ptr<MemoryBuffer> Input, bool IsMainFile,
                    unsigned NestingDepth, bool *ExitedMacro = nullptr) {
    std::string Filename = Input->getBufferIdentifier().str();
    SmallVector<AssemblySourceLine, 0> Lines;
    StringRef Remaining = Input->getBuffer();
    unsigned LineNumber = 0;
    while (!Remaining.empty()) {
      auto [Line, Rest] = Remaining.split('\n');
      Remaining = Rest;
      Line.consume_back("\r");
      Lines.push_back({Line.str(), Filename, ++LineNumber});
    }

    bool MacroExit = false;
    Error Err = processLines(
        Lines, IsMainFile ? InputKind::MainFile : InputKind::IncludeFile,
        NestingDepth, MacroExit, Filename);
    if (ExitedMacro)
      *ExitedMacro = MacroExit;
    return Err;
  }

  Error processLines(ArrayRef<AssemblySourceLine> Lines, InputKind Kind,
                     unsigned NestingDepth, bool &MacroExit,
                     StringRef InputFilename) {
    SmallVector<ControlFrame, 8> Controls;
    bool SawEnd = false;
    size_t LineIndex = 0;
    while (LineIndex != Lines.size()) {
      const AssemblySourceLine &Line = Lines[LineIndex];
      ListingLine = Kind == InputKind::Macro
                        ? ListingLine + 1
                        : std::max(ListingLine + 1, Line.Line);
      BuiltinVariables["INPUTFILE"] = VariableValue::string(Line.Filename);
      BuiltinVariables["LINENUM"] = VariableValue::arithmetic(Line.Line);
      bool Active = Controls.empty() || Controls.back().Active;
      std::string SubstitutedLine = prepareLine(Line);
      StringRef Statement = stripComment(SubstitutedLine).trim();
      StringRef Tail = Statement;
      StringRef First = takeToken(Tail);
      StringRef AfterFirst = Tail;
      StringRef Second = takeToken(AfterFirst);

      if (First.equals_insensitive("MACRO")) {
        if (!Tail.empty())
          return sourceError(Line, "unexpected tokens after MACRO directive");
        if (Error Err = defineMacro(Lines, LineIndex, Active,
                                    NestingDepth +
                                        static_cast<unsigned>(Controls.size())))
          return Err;
        continue;
      }

      bool IsIf = First.equals_insensitive("IF") || First == "[";
      if (IsIf) {
        if (NestingDepth + Controls.size() == 256)
          return sourceError(Line, "assembly control nesting limit exceeded");
        bool Condition = false;
        if (Active) {
          Expected<bool> Value = evaluateCondition(Tail);
          if (!Value)
            return sourceError(Line, toString(Value.takeError()));
          Condition = *Value;
        }
        Controls.push_back({ControlKind::If, Active, Active && Condition,
                            Active && Condition});
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("ELIF")) {
        if (Controls.empty() || Controls.back().Kind != ControlKind::If)
          return sourceError(Line, "ELIF directive without a matching IF");
        ControlFrame &Frame = Controls.back();
        if (Frame.SawElse)
          return sourceError(Line, "ELIF directive after ELSE");
        bool Condition = false;
        if (Frame.ParentActive) {
          Expected<bool> Value = evaluateCondition(Tail);
          if (!Value)
            return sourceError(Line, toString(Value.takeError()));
          Condition = *Value;
        }
        bool WasTaken = Frame.BranchTaken;
        Frame.Active = Frame.ParentActive && !WasTaken && Condition;
        Frame.BranchTaken |= Frame.ParentActive && Condition;
        ++LineIndex;
        continue;
      }

      bool IsElse = First.equals_insensitive("ELSE") || First == "|";
      if (IsElse) {
        if (Controls.empty() || Controls.back().Kind != ControlKind::If)
          return sourceError(Line, "ELSE directive without a matching IF");
        if (!Tail.empty())
          return sourceError(Line, "unexpected tokens after ELSE directive");
        ControlFrame &Frame = Controls.back();
        if (Frame.SawElse)
          return sourceError(Line,
                             "multiple ELSE directives in one IF structure");
        Frame.Active = Frame.ParentActive && !Frame.BranchTaken;
        Frame.BranchTaken = Frame.ParentActive;
        Frame.SawElse = true;
        ++LineIndex;
        continue;
      }

      bool IsEndIf = First.equals_insensitive("ENDIF") || First == "]";
      if (IsEndIf) {
        if (Controls.empty() || Controls.back().Kind != ControlKind::If)
          return sourceError(Line, "ENDIF directive without a matching IF");
        if (!Tail.empty())
          return sourceError(Line, "unexpected tokens after ENDIF directive");
        Controls.pop_back();
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("WHILE")) {
        if (NestingDepth + Controls.size() == 256)
          return sourceError(Line, "assembly control nesting limit exceeded");
        bool Condition = false;
        if (Active) {
          Expected<bool> Value = evaluateCondition(Tail);
          if (!Value)
            return sourceError(Line, toString(Value.takeError()));
          Condition = *Value;
        }
        ControlFrame Frame{ControlKind::While, Active, Active && Condition};
        Frame.WhileLine = LineIndex;
        Frame.BodyLine = LineIndex + 1;
        Controls.push_back(Frame);
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("WEND")) {
        if (Controls.empty() || Controls.back().Kind != ControlKind::While)
          return sourceError(Line, "WEND directive without a matching WHILE");
        if (!Tail.empty())
          return sourceError(Line, "unexpected tokens after WEND directive");
        ControlFrame &Frame = Controls.back();
        if (Frame.Active) {
          Expected<bool> Value = evaluateConditionLine(Lines[Frame.WhileLine]);
          if (!Value)
            return sourceError(Lines[Frame.WhileLine],
                               toString(Value.takeError()));
          if (*Value) {
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
        reportWarning(Line, 4038, "unimplemented directive entry");
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("ASSERT")) {
        SmallVector<StringRef, 2> Operands;
        splitOperands(Tail, Operands);
        if (Operands.front().empty())
          return sourceError(Line, "expected assertion expression");
        Expected<VariableValue> Value = evaluateExpression(Operands.front());
        if (!Value)
          return sourceError(Line, toString(Value.takeError()));
        if (Value->Kind != VariableKind::Logical)
          return sourceError(Line,
                             "A2067: illegal expression type; expected bool");
        if (!Value->Logical)
          return sourceError(Line, "A2170: assertion failed");
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("INFO") || First == "!") {
        SmallVector<StringRef, 3> Operands;
        splitOperands(Tail, Operands);
        if (Operands.size() != 2 || Operands[0].empty() || Operands[1].empty())
          return sourceError(Line, "A2003: improper line syntax");
        Expected<VariableValue> Condition = evaluateExpression(Operands[0]);
        if (!Condition)
          return sourceError(Line, toString(Condition.takeError()));
        if (Condition->Kind != VariableKind::Arithmetic)
          return sourceError(
              Line,
              "A2061: illegal expression type; expected absolute numeric");
        Expected<VariableValue> Message = evaluateExpression(Operands[1]);
        if (!Message)
          return sourceError(Line, toString(Message.takeError()));
        if (Message->Kind != VariableKind::String)
          return sourceError(Line,
                             "A2068: illegal expression type; expected string");
        if (Condition->Arithmetic)
          return sourceError(Line, "A2170: assertion failed");
        reportWarning(Line, 4058, Message->String);
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("MEXIT")) {
        if (!MacroParameters) {
          reportWarning(
              Line, 4094,
              "improper program syntax; unexpected MEND/MEXIT directive");
          ++LineIndex;
          continue;
        }
        if (!Tail.empty())
          return sourceError(Line, "unexpected tokens after MEXIT directive");
        MacroExit = true;
        return Error::success();
      }

      if (First.equals_insensitive("MEND")) {
        reportWarning(
            Line, 4094,
            "improper program syntax; unexpected MEND/MEXIT directive");
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("END")) {
        if (!Controls.empty())
          return sourceError(Line,
                             Controls.back().Kind == ControlKind::If
                                 ? "unexpected END before ENDIF directive"
                                 : "unexpected END before WEND directive");
        if (Kind == InputKind::Macro)
          return sourceError(Line, "END directive inside a macro expansion");
        if (Kind == InputKind::MainFile) {
          emitListingLineMarker(OS, ListingLine);
          emitLineMarker(OS, Line.Line, Line.Filename);
          OS << Line.Text << '\n';
        }
        SawEnd = true;
        break;
      }

      if (First.equals_insensitive("INCLUDE") ||
          First.equals_insensitive("GET")) {
        StringRef IncludedFilename = takeToken(Tail);
        if (IncludedFilename.empty() || !Tail.empty() ||
            IncludedFilename.front() == '"' || IncludedFilename.front() == '\'')
          return sourceError(Line, "expected include file name");

        if (NestingDepth + Controls.size() == 256)
          return sourceError(Line, "assembly control nesting limit exceeded");
        SmallString<256> IncludedPath;
        ErrorOr<std::unique_ptr<MemoryBuffer>> IncludedBuffer = openIncludeFile(
            IncludedFilename, Line.Filename, IncludeDirs, IncludedPath);
        if (!IncludedBuffer)
          return sourceError(
              Line, "unable to open include file '" + IncludedFilename +
                        "': " + IncludedBuffer.getError().message());
        bool IncludedMacroExit = false;
        if (Error Err = processFile(std::move(*IncludedBuffer),
                                    /*IsMainFile=*/false,
                                    NestingDepth + Controls.size() + 1,
                                    &IncludedMacroExit))
          return Err;
        if (IncludedMacroExit) {
          MacroExit = true;
          return Error::success();
        }
        ++LineIndex;
        continue;
      }

      auto Macro = Macros.find(First);
      bool HasInvocationLabel = false;
      StringRef InvocationLabel;
      StringRef ArgumentText = Tail;
      bool MayHaveInvocationLabel =
          !SubstitutedLine.empty() && !isSpace(SubstitutedLine.front());
      if (Macro == Macros.end() && MayHaveInvocationLabel && !Second.empty()) {
        Macro = Macros.find(Second);
        if (Macro != Macros.end()) {
          HasInvocationLabel = true;
          InvocationLabel = First;
          ArgumentText = AfterFirst;
        }
      }
      if (Macro != Macros.end()) {
        if (NestingDepth + Controls.size() == 256)
          return sourceError(Line, "assembly control nesting limit exceeded");
        std::string MacroName = Macro->getKey().str();
        MacroDefinition Definition = Macro->second;
        if (Error Err = expandMacro(Definition, MacroName, HasInvocationLabel,
                                    InvocationLabel, ArgumentText, Line,
                                    NestingDepth + Controls.size() + 1))
          return Err;
        ++LineIndex;
        continue;
      }

      if (First.equals_insensitive("AREA")) {
        SmallVector<StringRef, 8> Attributes;
        splitOperands(Tail, Attributes);
        if (!Attributes.empty()) {
          Expected<AreaName> Area = parseAreaName(Attributes.front());
          if (Area)
            BuiltinVariables["AREANAME"] =
                VariableValue::string(Area->Name.str());
          else
            consumeError(Area.takeError());
        }
      } else if (First.equals_insensitive("MAP") || First == "^") {
        SmallVector<StringRef, 2> Operands;
        splitOperands(Tail, Operands);
        Expected<VariableValue> Value = evaluateExpression(Operands.front());
        if (Value && Value->Kind == VariableKind::Arithmetic) {
          StorageMapOffset = Value->Arithmetic;
          BuiltinVariables["VAR"] =
              VariableValue::arithmetic(StorageMapOffset);
        } else if (!Value) {
          consumeError(Value.takeError());
        }
      } else if (First.equals_insensitive("FIELD") || First == "#" ||
                 Second.equals_insensitive("FIELD") || Second == "#") {
        StringRef Expression = First.equals_insensitive("FIELD") || First == "#"
                                   ? Tail
                                   : AfterFirst;
        Expected<VariableValue> Value = evaluateExpression(Expression);
        if (Value && Value->Kind == VariableKind::Arithmetic) {
          StorageMapOffset += Value->Arithmetic;
          BuiltinVariables["VAR"] =
              VariableValue::arithmetic(StorageMapOffset);
        } else if (!Value) {
          consumeError(Value.takeError());
        }
      }

      recordDefinedSymbols(SubstitutedLine, First, Second, Tail,
                           DefinedSymbols);

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
          return sourceError(Line, "expected one variable name after " + First);
        StringRef Name = unquoteIdentifier(NameToken);
        if (Constants.contains(Name))
          return sourceError(Line,
                             "variable name '" + Name + "' is already defined");
        if (IsLocalDeclaration) {
          if (!LocalVariables || !MacroParameters)
            return sourceError(
                Line, "local variables can only be declared within a macro");
          if (MacroParameters->contains(Name))
            return sourceError(Line, "local variable '" + Name +
                                         "' conflicts with a macro parameter");
          if (Error Err =
                  declareVariable(Name, *DeclarationKind, *LocalVariables))
            return sourceError(Line, toString(std::move(Err)));
          ++LineIndex;
          continue;
        }
        if (Error Err = declareVariable(Name, *DeclarationKind, Variables))
          return sourceError(Line, toString(std::move(Err)));
      } else if (isSetDirective(Second)) {
        StringRef Name = unquoteIdentifier(First);
        if (!isValidVariableName(First) || AfterFirst.empty())
          return sourceError(Line, "expected variable assignment expression");
        bool IsLocalAssignment =
            LocalVariables && LocalVariables->contains(Name);
        if (Error Err = assignCurrentVariable(Name, Second, AfterFirst))
          return sourceError(Line, toString(std::move(Err)));
        if (IsLocalAssignment) {
          ++LineIndex;
          continue;
        }
      } else if (isEquDirective(Second)) {
        StringRef Name = unquoteIdentifier(First);
        DefinedSymbols.insert(Name);
        VariableMap Effective = effectiveVariables();
        VariableExpressionParser Parser(AfterFirst, Effective, Constants,
                                        NoEscape, &DefinedSymbols,
                                        /*RegisterRelativeValues=*/nullptr,
                                        /*SymbolSizes=*/nullptr,
                                        &BuiltinVariables);
        Expected<VariableValue> Value = Parser.parse();
        if (Value && Value->Kind == VariableKind::Arithmetic)
          Constants[Name] = Value->Arithmetic;
        else if (!Value)
          consumeError(Value.takeError());
      }

      std::string EmittedLine = Line.Text;
      if (MacroParameters) {
        EmittedLine = SubstitutedLine;
        if (LocalVariables)
          EmittedLine =
              rewriteVariables(EmittedLine, *LocalVariables, NoEscape);
      }
      emitListingLineMarker(OS, ListingLine);
      emitLineMarker(OS, Line.Line, Line.Filename);
      OS << EmittedLine << '\n';
      ++LineIndex;
    }

    if (!Controls.empty()) {
      unsigned EndLine = Lines.empty() ? 1 : Lines.back().Line + 1;
      return sourceError(
          InputFilename, EndLine,
          Controls.back().Kind == ControlKind::If
              ? "unexpected end of file; missing ENDIF directive"
              : "unexpected end of file; missing WEND directive");
    }
    if (!SawEnd && Kind == InputKind::IncludeFile)
      return createStringError(
          Twine(InputFilename) +
          ": unexpected end of file; missing END directive");
    if (!SawEnd && Kind == InputKind::MainFile && !NoWarn &&
        !IgnoredWarnings.contains(4045))
      WithColor::warning(DiagOS, ProgName)
          << InputFilename << ": A4045: missing END directive\n";
    return Error::success();
  }

public:
  AssemblyControlExpander(ArrayRef<std::string> IncludeDirs, StringRef ProgName,
                          bool NoWarn, bool NoEscape,
                           const DenseSet<unsigned> &IgnoredWarnings,
                           raw_ostream &DiagOS, raw_ostream &OS,
                           VariableMap &Variables,
                           StringMap<uint64_t> &Constants, StringRef CommandLine)
      : IncludeDirs(IncludeDirs), ProgName(ProgName), NoWarn(NoWarn),
        NoEscape(NoEscape), IgnoredWarnings(IgnoredWarnings), DiagOS(DiagOS),
        OS(OS), Variables(Variables), Constants(Constants) {
    BuiltinVariables["AREANAME"] = VariableValue::string("");
    BuiltinVariables["COMMANDLINE"] =
        VariableValue::string(CommandLine.str());
    BuiltinVariables["INPUTFILE"] = VariableValue::string("");
    BuiltinVariables["LINENUM"] = VariableValue::arithmetic(0);
    BuiltinVariables["VAR"] = VariableValue::arithmetic(0);
  }

  Error run(std::unique_ptr<MemoryBuffer> Input) {
    return processFile(std::move(Input), /*IsMainFile=*/true,
                       /*NestingDepth=*/0);
  }
};

static Expected<std::unique_ptr<MemoryBuffer>>
expandAssemblyControl(std::unique_ptr<MemoryBuffer> Input,
                      ArrayRef<std::string> IncludeDirs, StringRef ProgName,
                      bool NoWarn, bool NoEscape,
                      const DenseSet<unsigned> &IgnoredWarnings,
                      raw_ostream &DiagOS, ArrayRef<std::string> Predefines,
                      StringRef CommandLine) {
  VariableMap Variables;
  StringMap<uint64_t> Constants;
  for (StringRef Predefine : Predefines)
    if (Error Err = executePredefine(Predefine, Variables, Constants, NoEscape))
      return createStringError(inconvertibleErrorCode(),
                               "invalid predefine '" + Predefine +
                                   "': " + toString(std::move(Err)));

  std::string Expanded;
  raw_string_ostream OS(Expanded);
  std::string Filename = Input->getBufferIdentifier().str();
  AssemblyControlExpander Expander(IncludeDirs, ProgName, NoWarn, NoEscape,
                                    IgnoredWarnings, DiagOS, OS, Variables,
                                    Constants, CommandLine);
  if (Error Err = Expander.run(std::move(Input)))
    return std::move(Err);
  return MemoryBuffer::getMemBufferCopy(Expanded, Filename);
}

struct DebugOptions {
  bool Enabled = false;
  bool UseSHA1 = false;
  std::string ObjectFilename;
  std::string SourceLink;
};

struct DebugFile {
  std::string Filename;
  std::string Checksum;
  unsigned Number;
};

class LiteralPool {
  struct Entry {
    std::string Label;
    std::string Expression;
    unsigned Size;
  };

  raw_ostream &OS;
  SmallVector<Entry, 4> Entries;
  StringMap<std::string> Labels;
  unsigned NextLabel = 0;

public:
  explicit LiteralPool(raw_ostream &OS) : OS(OS) {}

  std::string add(StringRef Expression, unsigned Size) {
    std::string Key = (Twine(Size) + ":" + Expression).str();
    auto Existing = Labels.find(Key);
    if (Existing != Labels.end())
      return Existing->second;

    std::string Label =
        (Twine(".Larmasm64_literal_") + Twine(NextLabel++)).str();
    Entries.push_back({Label, Expression.str(), Size});
    Labels[Key] = Label;
    return Label;
  }

  void emit() {
    if (Entries.empty())
      return;

    bool Has64BitEntry = llvm::any_of(
        Entries, [](const Entry &Entry) { return Entry.Size == 8; });
    OS << ".balign " << (Has64BitEntry ? 8 : 4) << ", 0\n";

    // ARMASM64 emits 64-bit entries before 32-bit entries. Its 64-bit list is
    // assembled in reverse encounter order, while the 32-bit list is not.
    for (const Entry &Entry : llvm::reverse(Entries))
      if (Entry.Size == 8)
        OS << Entry.Label << ":; .quad " << Entry.Expression << '\n';
    for (const Entry &Entry : Entries)
      if (Entry.Size == 4)
        OS << Entry.Label << ":; .long " << Entry.Expression << '\n';

    Entries.clear();
    Labels.clear();
  }
};

static std::string normalizeDebugPath(StringRef Filename) {
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

static Expected<SmallVector<DebugFile, 4>>
collectDebugFiles(StringRef ExpandedInput, StringRef MainFilename,
                  bool UseSHA1) {
  SmallVector<std::string, 4> Filenames;
  StringSet<> Seen;
  while (!ExpandedInput.empty()) {
    auto [Line, Rest] = ExpandedInput.split('\n');
    ExpandedInput = Rest;
    Line.consume_back("\r");
    StringRef Marker = Line.trim();
    if (!Marker.consume_front("# "))
      continue;
    StringRef LineNumber = takeToken(Marker);
    unsigned ParsedLine;
    Marker = Marker.trim();
    if (LineNumber.getAsInteger(10, ParsedLine) ||
        !Marker.consume_front("\"") || !Marker.consume_back("\""))
      continue;
    std::string Filename = normalizeDebugPath(Marker);
    if (Seen.insert(Filename).second)
      Filenames.push_back(std::move(Filename));
  }

  std::string MainPath = normalizeDebugPath(MainFilename);
  llvm::erase(Filenames, MainPath);
  Filenames.push_back(MainPath);

  SmallVector<DebugFile, 4> Files;
  for (auto [Index, Filename] : llvm::enumerate(Filenames)) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
        MemoryBuffer::getFile(Filename, /*IsText=*/false);
    if (!Buffer) {
      if (Filename == "<stdin>") {
        Files.push_back({Filename, {}, static_cast<unsigned>(Index + 1)});
        continue;
      }
      return createStringError(Buffer.getError(), "can't open file: %s",
                               Filename.c_str());
    }
    StringRef Contents = (*Buffer)->getBuffer();
    ArrayRef<uint8_t> Bytes(reinterpret_cast<const uint8_t *>(Contents.data()),
                            Contents.size());
    std::string Checksum =
        UseSHA1 ? toHex(SHA1::hash(Bytes)) : toHex(SHA256::hash(Bytes));
    Files.push_back(
        {Filename, std::move(Checksum), static_cast<unsigned>(Index + 1)});
  }
  return Files;
}

static Expected<std::unique_ptr<MemoryBuffer>>
translateInput(std::unique_ptr<MemoryBuffer> Input,
               ArrayRef<std::string> IncludeDirs, StringRef ProgName,
               bool NoWarn, bool NoEscape,
               const DenseSet<unsigned> &IgnoredWarnings, raw_ostream &DiagOS,
               ArrayRef<std::string> Predefines, StringRef CommandLine,
               const DebugOptions &Debug) {
  SmallVector<DebugFile, 4> DebugFiles;
  StringMap<unsigned> DebugFileNumbers;
  if (Debug.Enabled) {
    Expected<SmallVector<DebugFile, 4>> Files = collectDebugFiles(
        Input->getBuffer(), Input->getBufferIdentifier(), Debug.UseSHA1);
    if (!Files)
      return Files.takeError();
    DebugFiles = std::move(*Files);
    for (const DebugFile &File : DebugFiles)
      DebugFileNumbers[File.Filename] = File.Number;
  }

  StringRef Remaining = Input->getBuffer();
  std::string Translated;
  raw_string_ostream OS(Translated);
  OS << ".def @comp.id; .scl 3; .endef; .set @comp.id, 17010072\n"
        ".def @feat.00; .scl 3; .endef; .set @feat.00, 16\n";
  for (const DebugFile &File : DebugFiles) {
    OS << ".cv_file " << File.Number << ' '
       << quoteAssemblyString(File.Filename);
    if (!File.Checksum.empty())
      OS << ' ' << quoteAssemblyString(File.Checksum) << ' '
         << (Debug.UseSHA1
                 ? static_cast<unsigned>(codeview::FileChecksumKind::SHA1)
                 : static_cast<unsigned>(codeview::FileChecksumKind::SHA256));
    OS << '\n';
  }
  StringMap<std::string> Constants;
  StringMap<uint64_t> AbsoluteConstants;
  VariableMap Variables;
  VariableMap BuiltinVariables;
  ExportMap Exports;
  StringMap<std::pair<std::string, unsigned>> ExportLocations;
  StringSet<> DefinedSymbols;
  StringSet<> DefinedObjectSymbols;
  StringSet<> ExternalSymbols;
  StringSet<> CommonSymbols;
  StringSet<> SeenAreas;
  StringMap<std::string> AreaBaseSymbols;
  StringMap<uint64_t> AreaAlignments;
  StringMap<bool> AreaCodeAlignments;
  StringMap<bool> AreaIsCode;
  StringMap<bool> AreaIsNoInit;
  StringMap<std::string> AreaFlags;
  StringMap<std::string> AreaSectionSuffixes;
  StringMap<std::string> AreaAttributeSignatures;
  unsigned AreaBaseSymbolCount = 0;
  unsigned AlignSymbolCount = 0;
  RegisterAliasMap RegisterAliases;
  StorageMapField CurrentStorageMap;
  StorageMapFieldMap StorageMapFields;
  RegisterRelativeMap RegisterRelativeValues;
  StringMap<uint64_t> SymbolSizes;
  struct DebugCodeSection {
    std::string Name;
    std::string Flags;
    std::string Suffix;
    std::string StartSymbol;
    std::string EndSymbol;
    unsigned FunctionId;
    bool HasLines = false;
  };
  SmallVector<DebugCodeSection, 4> DebugCodeSections;
  StringMap<unsigned> DebugCodeSectionIndices;
  std::optional<unsigned> CurrentDebugCodeSection;
  struct DebugSymbol {
    std::string Name;
    std::string AssemblerName;
    std::string EndSymbol;
    bool IsProcedure;
    bool IsExternal;
  };
  SmallVector<DebugSymbol, 16> DebugSymbols;
  StringSet<> DebugSymbolNames;
  std::optional<unsigned> ActiveDebugProcedure;
  unsigned DebugEndSymbolCount = 0;
  unsigned DebugLabelSymbolCount = 0;
  struct PendingWeakExternal {
    std::string Name;
    std::string Fallback;
    uint64_t Search = COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS;
    bool Conditional = false;
  };
  SmallVector<PendingWeakExternal, 4> PendingWeakExternals;
  SmallVector<std::pair<size_t, size_t>, 16> PendingSymbolUses;
  struct PreviousDataDefinition {
    size_t ExpressionOffset;
    size_t ExpressionLength;
    unsigned Size;
    std::string Expression;
    bool IsSymbolic;
    bool ReplaceWithCurrentLocation = false;
    bool IsInstruction = false;
  };
  std::optional<PreviousDataDefinition> PreviousData;
  struct RelocatedExpression {
    size_t Offset;
    size_t Length;
    bool ReplaceWithCurrentLocation;
  };
  SmallVector<RelocatedExpression, 4> RelocatedExpressions;
  LiteralPool Literals(OS);
  collectExports(Remaining, Exports);

  for (StringRef Predefine : Predefines)
    if (Error Err =
            executePredefine(Predefine, Variables, AbsoluteConstants, NoEscape))
      return createStringError(inconvertibleErrorCode(),
                                "invalid predefine '" + Predefine +
                                    "': " + toString(std::move(Err)));

  BuiltinVariables["AREANAME"] = VariableValue::string("");
  BuiltinVariables["COMMANDLINE"] = VariableValue::string(CommandLine.str());
  BuiltinVariables["INPUTFILE"] =
      VariableValue::string(Input->getBufferIdentifier().str());
  BuiltinVariables["LINENUM"] = VariableValue::arithmetic(0);
  BuiltinVariables["VAR"] = VariableValue::arithmetic(0);

  VariableMap SizeVariables = Variables;
  StringMap<uint64_t> SizeConstants = AbsoluteConstants;
  StringRef SizeRemaining = Remaining;
  while (!SizeRemaining.empty()) {
    auto [SourceLine, Rest] = SizeRemaining.split('\n');
    SizeRemaining = Rest;
    SourceLine.consume_back("\r");
    bool HasLabel = !SourceLine.empty() && !isSpace(SourceLine.front());
    std::string Substituted = substituteVariables(SourceLine, SizeVariables);
    StringRef SizeTail = stripComment(Substituted).trim();
    StringRef SizeFirst = takeToken(SizeTail);
    StringRef SizeAfterFirst = SizeTail;
    StringRef SizeSecond = takeToken(SizeAfterFirst);
    if (SizeFirst.empty() || SizeFirst == "#")
      continue;

    std::optional<VariableKind> DeclarationKind;
    if (SizeFirst.equals_insensitive("GBLA"))
      DeclarationKind = VariableKind::Arithmetic;
    else if (SizeFirst.equals_insensitive("GBLL"))
      DeclarationKind = VariableKind::Logical;
    else if (SizeFirst.equals_insensitive("GBLS"))
      DeclarationKind = VariableKind::String;
    if (DeclarationKind) {
      StringRef Name = unquoteIdentifier(takeToken(SizeTail));
      if (!Name.empty())
        consumeError(declareVariable(Name, *DeclarationKind, SizeVariables));
      continue;
    }
    if (isSetDirective(SizeSecond)) {
      StringRef Name = unquoteIdentifier(SizeFirst);
      consumeError(assignVariable(Name, SizeSecond, SizeAfterFirst,
                                  /*ImplicitDeclaration=*/false, SizeVariables,
                                  SizeConstants, NoEscape));
      continue;
    }
    if (isEquDirective(SizeSecond)) {
      VariableExpressionParser Parser(SizeAfterFirst, SizeVariables,
                                      SizeConstants, NoEscape);
      Expected<VariableValue> Value = Parser.parse();
      if (Value && Value->Kind == VariableKind::Arithmetic)
        SizeConstants[unquoteIdentifier(SizeFirst)] = Value->Arithmetic;
      else if (!Value)
        consumeError(Value.takeError());
    }
    if (!HasLabel)
      continue;

    StringRef Name = unquoteIdentifier(SizeFirst);
    uint64_t Size = 0;
    if (isDataDirective(SizeSecond)) {
      SmallVector<StringRef, 8> Operands;
      splitOperands(SizeAfterFirst, Operands);
      bool IsByte = SizeSecond.equals_insensitive("DCB") || SizeSecond == "=";
      unsigned ElementSize =
          IsByte                                               ? 1
          : SizeSecond.equals_insensitive("DCW") ||
                  SizeSecond.equals_insensitive("DCWU")        ? 2
          : SizeSecond.equals_insensitive("DCD") ||
                  SizeSecond.equals_insensitive("DCDU") ||
                  SizeSecond == "&" ||
                  SizeSecond.equals_insensitive("DCI") ||
                  SizeSecond.equals_insensitive("DCI.W")       ? 4
                                                               : 8;
      for (StringRef Operand : Operands) {
        if (IsByte) {
          VariableExpressionParser Parser(Operand, SizeVariables,
                                          SizeConstants, NoEscape);
          Expected<VariableValue> Value = Parser.parse();
          if (Value && Value->Kind == VariableKind::String) {
            Size += Value->String.size();
            continue;
          }
          if (!Value)
            consumeError(Value.takeError());
        }
        Size += ElementSize;
      }
    } else if (isStorageDirective(SizeSecond)) {
      SmallVector<StringRef, 3> Operands;
      splitOperands(SizeAfterFirst, Operands);
      VariableExpressionParser Parser(Operands.front(), SizeVariables,
                                      SizeConstants, NoEscape);
      Expected<VariableValue> Value = Parser.parse();
      if (Value && Value->Kind == VariableKind::Arithmetic)
        Size = Value->Arithmetic;
      else if (!Value)
        consumeError(Value.takeError());
    } else if (SizeSecond.equals_insensitive("FIELD")) {
      VariableExpressionParser Parser(SizeAfterFirst, SizeVariables,
                                      SizeConstants, NoEscape);
      Expected<VariableValue> Value = Parser.parse();
      if (Value && Value->Kind == VariableKind::Arithmetic)
        Size = Value->Arithmetic;
      else if (!Value)
        consumeError(Value.takeError());
    } else if (SizeSecond.equals_insensitive("ADRL")) {
      Size = 8;
    } else if (!SizeSecond.empty() &&
               !isEquDirective(SizeSecond) &&
               !SizeSecond.equals_insensitive("PROC") &&
               !SizeSecond.equals_insensitive("FUNCTION") &&
               !SizeSecond.equals_insensitive("ROUT")) {
      Size = 4;
    }
    SymbolSizes[Name] = Size;
  }

  std::string CurrentFilename = Input->getBufferIdentifier().str();
  unsigned CurrentLine = 0;
  unsigned CurrentListingLine = 0;
  unsigned PCSymbolCount = 0;
  bool CurrentAreaIsCode = false;
  bool CurrentAreaIsNoInit = false;
  bool CurrentAreaUsesCodeAlignment = false;
  uint64_t CurrentAreaAlignment = 8;
  std::string CurrentAreaName;
  std::string CurrentAreaBaseSymbol;
  std::optional<std::string> ActiveProcedureArea;
  bool AtProcedureStart = false;
  unsigned NumericLabelScope = 0;
  std::string NumericLabelRoutName;
  DenseSet<unsigned> DefinedNumericLabels;
  auto EnsureDefaultSection = [&]() {
    if (!CurrentAreaName.empty())
      return;
    CurrentAreaName = "__DefaultSection";
    CurrentAreaIsCode = false;
    CurrentAreaIsNoInit = false;
    CurrentAreaUsesCodeAlignment = false;
    CurrentAreaAlignment = 8;
    std::string AreaKey = "__DefaultSection{}";
    SeenAreas.insert(AreaKey);
    AreaAlignments[AreaKey] = CurrentAreaAlignment;
    AreaCodeAlignments[AreaKey] = false;
    AreaIsCode[AreaKey] = false;
    AreaIsNoInit[AreaKey] = false;
    AreaFlags[AreaKey] = "dw";
    AreaSectionSuffixes[AreaKey] = "";
    AreaAttributeSignatures[AreaKey] = "data,readwrite,align=3";
    CurrentAreaBaseSymbol =
        (Twine(".Larmasm64_area_") + Twine(AreaBaseSymbolCount++)).str();
    AreaBaseSymbols[AreaKey] = CurrentAreaBaseSymbol;
    OS << ".section \"__DefaultSection\",\"dw\"; .p2align 3; "
       << CurrentAreaBaseSymbol << ":\n";
  };
  auto EmitDebugLocation = [&]() {
    if (!Debug.Enabled || !CurrentAreaIsCode || !CurrentDebugCodeSection)
      return;
    auto File = DebugFileNumbers.find(normalizeDebugPath(CurrentFilename));
    if (File == DebugFileNumbers.end())
      return;
    DebugCodeSection &Section = DebugCodeSections[*CurrentDebugCodeSection];
    OS << "\n.cv_loc " << Section.FunctionId << ' ' << File->second << ' '
       << CurrentLine << " 0 is_stmt 1\n";
    Section.HasLines = true;
  };
  auto RecordDebugLabel = [&](StringRef Name) {
    if (!Debug.Enabled || !CurrentAreaIsCode ||
        !DebugSymbolNames.insert(Name).second)
      return;
    std::string RelocationName =
        (Twine(".Larmasm64_debug_label_") + Twine(DebugLabelSymbolCount++))
            .str();
    OS << "; " << RelocationName << ':';
    DebugSymbols.push_back({Name.str(),
                            std::move(RelocationName),
                            {},
                            /*IsProcedure=*/false,
                            /*IsExternal=*/Exports.contains(Name)});
  };

  while (!Remaining.empty()) {
    auto [Line, Rest] = Remaining.split('\n');
    Remaining = Rest;
    Line.consume_back("\r");
    ++CurrentLine;

    BuiltinVariables["AREANAME"] =
        VariableValue::string(CurrentAreaName);
    BuiltinVariables["INPUTFILE"] = VariableValue::string(CurrentFilename);
    BuiltinVariables["LINENUM"] = VariableValue::arithmetic(CurrentLine);
    BuiltinVariables["VAR"] =
        VariableValue::arithmetic(CurrentStorageMap.Offset);

    std::string SubstitutedLine = substituteVariables(Line, Variables);
    Line = stripComment(SubstitutedLine);
    StringRef Statement = Line.trim();
    StringRef Tail = Statement;
    StringRef First = takeToken(Tail);
    StringRef AfterFirst = Tail;
    StringRef Second = takeToken(AfterFirst);

    bool FirstWasDefined = DefinedSymbols.contains(unquoteIdentifier(First));
    recordDefinedSymbols(Line, First, Second, Tail, DefinedSymbols);

    auto SourceError = [&](const Twine &Message) -> Error {
      return createStringError(inconvertibleErrorCode(),
                               Twine(CurrentFilename) + ":" +
                                   Twine(CurrentLine) + ": " + Message);
    };
    auto SymbolConflict = [&](StringRef Name) -> Error {
      return SourceError("A2026: multiple symbol definition or "
                         "incompatibility: " +
                         Name);
    };
    auto HasInternalSymbol = [&](StringRef Name) {
      return RegisterAliases.contains(Name) || StorageMapFields.contains(Name);
    };
    auto ConflictsWithObjectDefinition = [&](StringRef Name) {
      return HasInternalSymbol(Name) || ExternalSymbols.contains(Name) ||
             CommonSymbols.contains(Name);
    };
    auto Evaluate = [&](StringRef Expression) -> Expected<VariableValue> {
      VariableExpressionParser Parser(Expression, Variables, AbsoluteConstants,
                                       NoEscape, &DefinedSymbols,
                                       &RegisterRelativeValues, &SymbolSizes,
                                       &BuiltinVariables);
      return Parser.parse();
    };
    auto EvaluateAbsolute = [&](StringRef Expression) -> Expected<uint64_t> {
      Expected<VariableValue> Value = Evaluate(Expression);
      if (!Value)
        return Value.takeError();
      if (Value->Kind != VariableKind::Arithmetic)
        return createStringError(inconvertibleErrorCode(),
                                 "expected absolute numeric expression");
      return Value->Arithmetic;
    };

    if (First == "#") {
      StringRef Marker = Tail;
      StringRef MarkerKind = takeToken(Marker);
      if (MarkerKind == "armasm64_listing") {
        if (Marker.getAsInteger(10, CurrentListingLine))
          return SourceError("invalid internal listing line marker");
        continue;
      }
      Marker = Tail;
      StringRef SourceLine = takeToken(Marker);
      unsigned ParsedLine;
      Marker = Marker.trim();
      if (!SourceLine.getAsInteger(10, ParsedLine) &&
          Marker.consume_front("\"") && Marker.consume_back("\"")) {
        CurrentFilename = Marker.str();
        CurrentLine = ParsedLine - 1;
        OS << Line << '\n';
        continue;
      }
    }
    if (First.equals_insensitive("#line")) {
      OS << Line << '\n';
      continue;
    }

    bool IsRelocDirective = First.equals_insensitive("RELOC");
    if (!First.empty() && !IsRelocDirective)
      PreviousData.reset();

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
      if (HasInternalSymbol(Name))
        return SymbolConflict(Name);
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
      if (Error Err =
               assignVariable(Name, Second, AfterFirst,
                              /*ImplicitDeclaration=*/false, Variables,
                              AbsoluteConstants, NoEscape,
                              /*ExpressionVariables=*/nullptr, &DefinedSymbols,
                              &BuiltinVariables))
        return SourceError(toString(std::move(Err)));
      OS << '\n';
      continue;
    }

    if (std::optional<RegisterAliasKind> AliasKind =
            registerAliasKindForDirective(Second)) {
      StringRef Name = unquoteIdentifier(First);
      if (!isValidVariableName(First) || AfterFirst.empty())
        return SourceError("A2173: syntax error in expression");
      auto Existing = RegisterAliases.find(Name);
      if (Variables.contains(Name) || Constants.contains(Name) ||
          DefinedSymbols.contains(Name) || StorageMapFields.contains(Name) ||
          (Existing != RegisterAliases.end() &&
           Existing->second.Kind != *AliasKind))
        return SymbolConflict(Name);
      if (std::optional<std::pair<RegisterAliasKind, unsigned>> Register =
              parseExtensionRegisterName(Name)) {
        if (Register->first != *AliasKind)
          return SymbolConflict(Name);
      } else if (isPredefinedRegisterName(Name)) {
        return SymbolConflict(Name);
      }

      Expected<unsigned> Target = parseRegisterAliasTarget(
          AfterFirst, *AliasKind, RegisterAliases, NoEscape);
      if (!Target)
        return SourceError(toString(Target.takeError()));

      RegisterAliases[Name] = RegisterAlias{*AliasKind, *Target};
      OS << '\n';
      continue;
    }
    if (Second.equals_insensitive("RN"))
      return SourceError("A2034: unknown opcode: RN");

    if (First.equals_insensitive("MAP") || First == "^") {
      SmallVector<StringRef, 2> Operands;
      splitOperands(Tail, Operands);
      if (Operands.empty() || Operands.size() > 2 || Operands[0].empty() ||
          (Operands.size() == 2 && Operands[1].empty()))
        return SourceError("A2003: improper line syntax");
      Expected<uint64_t> Offset = EvaluateAbsolute(Operands[0]);
      if (!Offset)
        return SourceError(toString(Offset.takeError()));
      CurrentStorageMap.Offset = static_cast<int64_t>(*Offset);
      CurrentStorageMap.BaseRegister =
          Operands.size() == 2
              ? std::optional<std::string>(Operands[1].trim().str())
              : std::nullopt;
      OS << '\n';
      continue;
    }

    bool IsField = First.equals_insensitive("FIELD") || First == "#" ||
                   Second.equals_insensitive("FIELD") || Second == "#";
    if (IsField) {
      bool HasLabel = !First.equals_insensitive("FIELD") && First != "#";
      StringRef Expression = HasLabel ? AfterFirst : Tail;
      Expected<uint64_t> Size = EvaluateAbsolute(Expression);
      if (!Size)
        return SourceError(toString(Size.takeError()));
      int64_t SignedSize = static_cast<int64_t>(*Size);
      if (SignedSize < 0)
        return SourceError("A2209: Immediate value " + Twine(SignedSize) +
                           " out of range");

      if (HasLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (!isValidVariableName(First) || HasInternalSymbol(Name) ||
            Variables.contains(Name) || Constants.contains(Name) ||
            FirstWasDefined || isPredefinedRegisterName(Name))
          return SymbolConflict(Name);
        StorageMapFields[Name] = CurrentStorageMap;
        SymbolSizes[Name] = *Size;
        if (CurrentStorageMap.BaseRegister)
          if (std::optional<unsigned> Base =
                  getStorageMapBaseEncoding(*CurrentStorageMap.BaseRegister))
            RegisterRelativeValues[Name] =
                VariableValue::registerRelative(*Base, 0);
      }
      if (CurrentStorageMap.Offset > INT64_MAX - SignedSize)
        return SourceError("storage map offset overflow");
      CurrentStorageMap.Offset += SignedSize;
      OS << '\n';
      continue;
    }

    std::string PCSymbol = (Twine("\"|") + Twine(PCSymbolCount) + "\"").str();
    bool UsesPC = false;
    std::string RewrittenLine =
        rewriteVariables(Line, Variables, NoEscape, PCSymbol, &UsesPC,
                         &BuiltinVariables);
    if (UsesPC)
      ++PCSymbolCount;
    Line = RewrittenLine;
    std::string NumericLabelToken;
    std::optional<NumericLocalLabel> NumericLabel =
        parseNumericLocalLabel(First);
    if (NumericLabel) {
      if (!NumericLabel->RoutName.empty() &&
          NumericLabel->RoutName != NumericLabelRoutName)
        return SourceError("numeric local label ROUT name mismatch");
      DefinedNumericLabels.insert(NumericLabel->Number);
    }
    Expected<std::string> NumericRewritten = rewriteNumericLocalLabelReferences(
        Line, NumericLabelScope, NumericLabelRoutName, DefinedNumericLabels);
    if (!NumericRewritten)
      return SourceError(toString(NumericRewritten.takeError()));
    Line = *NumericRewritten;
    Statement = Line.trim();
    Tail = Statement;
    First = takeToken(Tail);
    AfterFirst = Tail;
    Second = takeToken(AfterFirst);
    NumericLabel = parseNumericLocalLabel(First);
    if (NumericLabel) {
      NumericLabelToken =
          std::to_string(NumericLabelScope * 100 + NumericLabel->Number);
      First = NumericLabelToken;
    }
    auto EmitNumericLabel = [&]() {
      assert(NumericLabel && "expected numeric local label");
      std::string SymbolName;
      raw_string_ostream SymbolOS(SymbolName);
      SymbolOS << "_lc" << format("%03u", NumericLabel->Number) << '_'
               << format("%06u", CurrentListingLine) << '_';
      OS << ".def " << SymbolName << "; .scl 6; .endef; " << SymbolName
         << ":; " << First << ':';
    };
    bool IsDataLine = isDataDirective(First) || isDataDirective(Second);
    bool IsStorageLine =
        isStorageDirective(First) || isStorageDirective(Second);
    auto EmitPCLabel = [&]() {
      OS << ".def " << PCSymbol << "; .scl 6; .endef; " << PCSymbol << ":; ";
    };
    if (UsesPC && !IsDataLine)
      EmitPCLabel();

    if (First.equals_insensitive("EXPORT") ||
        First.equals_insensitive("GLOBAL")) {
      Expected<ExportSpec> Spec = parseExportSpec(Tail);
      if (!Spec)
        return SourceError(toString(Spec.takeError()));
      StringRef Name = Spec->Name;
      if (HasInternalSymbol(Name))
        return SymbolConflict(Name);
      Exports[Name] = Spec->Type;
      ExportLocations[Name] = {CurrentFilename, CurrentLine};
      std::string AssemblerName = getAssemblerSymbolName(Name);
      OS << ".def " << AssemblerName << "; .scl 2; .type "
         << (Spec->Type == ExportType::Function ? 32 : 0) << "; .endef; .globl "
         << AssemblerName;
    } else if (First.equals_insensitive("ALIAS")) {
      SmallVector<StringRef, 2> Operands;
      splitOperands(Tail, Operands);
      if (Operands.size() != 2 || Operands[0].empty() || Operands[1].empty())
        return SourceError("A2003: improper line syntax");
      StringRef Original = unquoteIdentifier(Operands[0]);
      StringRef Alias = unquoteIdentifier(Operands[1]);
      if (!DefinedObjectSymbols.contains(Original))
        return SourceError("A2249: symbol '" + Original +
                           "' is undefined or external");
      if (Alias.empty() || DefinedObjectSymbols.contains(Alias) ||
          ConflictsWithObjectDefinition(Alias))
        return SymbolConflict(Alias);
      DefinedObjectSymbols.insert(Alias);
      std::string AssemblerAlias = getAssemblerSymbolName(Alias);
      if (!Exports.contains(Alias))
        OS << ".def " << AssemblerAlias << "; .scl 3; .endef; ";
      OS << ".set " << AssemblerAlias << ", "
         << getAssemblerSymbolName(Original);
    } else if (First.equals_insensitive("IMPORT") ||
               First.equals_insensitive("EXTERN")) {
      bool Conditional = First.equals_insensitive("EXTERN");
      SmallVector<StringRef, 4> Operands;
      splitOperands(Tail, Operands);
      if (Operands.empty() || Operands.size() > 3 || Operands[0].empty())
        return SourceError("A2003: improper line syntax");

      StringRef Name = unquoteIdentifier(Operands[0]);
      if (Name.empty() || HasInternalSymbol(Name) ||
          DefinedObjectSymbols.contains(Name) || CommonSymbols.contains(Name) ||
          ExternalSymbols.contains(Name))
        return SymbolConflict(Name);
      ExternalSymbols.insert(Name);
      std::string AssemblerName = getAssemblerSymbolName(Name);

      if (Operands.size() == 1) {
        // Unlike IMPORT, an unused EXTERN does not appear in the object.
        if (!Conditional)
          OS << ".globl " << AssemblerName;
      } else {
        StringRef WeakOperand = Operands[1].trim();
        StringRef Weak = takeToken(WeakOperand);
        StringRef Fallback = unquoteIdentifier(takeToken(WeakOperand));
        if (!Weak.equals_insensitive("WEAK"))
          return SourceError("A2135: attribute does not pertain to a "
                             "relocatable module; ignored, weak expected");
        if (Fallback.empty() || !WeakOperand.empty())
          return SourceError("A2003: improper line syntax");
        if (Fallback == Name || HasInternalSymbol(Fallback) ||
            DefinedObjectSymbols.contains(Fallback) ||
            CommonSymbols.contains(Fallback))
          return SymbolConflict(Fallback);

        uint64_t Search = COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS;
        if (Operands.size() == 3) {
          StringRef TypeOperand = Operands[2].trim();
          StringRef Type = takeToken(TypeOperand);
          if (!Type.equals_insensitive("TYPE") || TypeOperand.empty())
            return SourceError("A2003: improper line syntax");
          Expected<uint64_t> Value = EvaluateAbsolute(TypeOperand);
          if (!Value)
            return SourceError(toString(Value.takeError()));
          Search = *Value;
          if ((Search < COFF::IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY ||
               Search > COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS) &&
              !NoWarn && !IgnoredWarnings.contains(4069))
            WithColor::warning(DiagOS, ProgName)
                << CurrentFilename << ":" << CurrentLine
                << ": A4069: immediate value " << static_cast<int64_t>(Search)
                << " out of range; expected values: 1,2,3\n";
          Search &= 7;
        }

        ExternalSymbols.insert(Fallback);
        OS << ".globl " << getAssemblerSymbolName(Fallback);
        PendingWeakExternals.push_back(
            {Name.str(), Fallback.str(), Search, Conditional});
      }
    } else if (First.equals_insensitive("COMMON")) {
      SmallVector<StringRef, 3> Operands;
      splitOperands(Tail, Operands);
      if (Operands.empty() || Operands[0].empty())
        return SourceError("A2003: improper line syntax: End of line");
      if (Operands.size() > 2)
        return SourceError("A2221: The COMMON directive takes two parameters; "
                           "specifying an alignment is not supported");

      StringRef Name = unquoteIdentifier(Operands[0]);
      if (Name.empty() || HasInternalSymbol(Name) ||
          DefinedObjectSymbols.contains(Name) ||
          ExternalSymbols.contains(Name) || CommonSymbols.contains(Name))
        return SymbolConflict(Name);
      uint64_t Size = 0;
      if (Operands.size() == 2) {
        Expected<uint64_t> Value = EvaluateAbsolute(Operands[1]);
        if (!Value)
          return SourceError(toString(Value.takeError()));
        Size = *Value;
      }
      int64_t SignedSize = static_cast<int64_t>(Size);
      if (SignedSize < 0 || Size > UINT32_MAX)
        return SourceError("A2209: Immediate value " + Twine(SignedSize) +
                           " out of range");
      CommonSymbols.insert(Name);
      EnsureDefaultSection();
      std::string AssemblerName = getAssemblerSymbolName(Name);
      if (Size == 0)
        OS << ".globl " << AssemblerName;
      else
        OS << ".armasm64_common " << AssemblerName << ", " << Size;
    } else if (First.equals_insensitive("RELOC")) {
      SmallVector<StringRef, 2> Operands;
      splitOperands(Tail, Operands);
      if (Operands.empty() || Operands.size() > 2 || Operands[0].empty() ||
          (Operands.size() == 2 && Operands[1].empty()))
        return SourceError("A2003: improper line syntax");
      if (!PreviousData)
        return SourceError("A2203: RELOC directive must follow an instruction "
                           "or data definition directive");

      Expected<uint64_t> Type = EvaluateAbsolute(Operands[0]);
      if (!Type)
        return SourceError(toString(Type.takeError()));
      StringRef RelocationName = getARM64RelocationName(*Type);
      if (RelocationName.empty())
        return SourceError("A2204: Unknown relocation type " + Twine(*Type));
      unsigned RelocationSize =
          *Type == COFF::IMAGE_REL_ARM64_ADDR64    ? 8
          : *Type == COFF::IMAGE_REL_ARM64_SECTION ? 2
          : *Type == COFF::IMAGE_REL_ARM64_ABSOLUTE ? 0
                                                    : 4;
      if (!PreviousData->IsInstruction &&
          PreviousData->Size < RelocationSize && !NoWarn &&
          !IgnoredWarnings.contains(4205))
        WithColor::warning(DiagOS, ProgName)
            << CurrentFilename << ":" << CurrentLine
            << ": A4205: Previous data definition too small for requested "
               "relocation; emitting anyway\n";

      std::string Expression;
      if (Operands.size() == 2) {
        Expression = normalizeSymbolicExpression(Operands[1], Constants);
      } else if (PreviousData->IsSymbolic) {
        Expression = PreviousData->Expression;
      } else {
        return SourceError("A2206: Must specify a relocation target");
      }
      if (PreviousData->IsSymbolic)
        RelocatedExpressions.push_back(
            {PreviousData->ExpressionOffset, PreviousData->ExpressionLength,
             PreviousData->ReplaceWithCurrentLocation});
      OS << ".reloc . - " << PreviousData->Size << ", " << RelocationName
         << ", " << Expression;
      PreviousData.reset();
    } else if (First.equals_insensitive("LTORG")) {
      if (!Tail.empty())
        return SourceError("A2003: improper line syntax");
      Literals.emit();
    } else if (First.equals_insensitive("INCBIN")) {
      StringRef IncludedFilename = takeToken(Tail);
      if (IncludedFilename.empty() || !Tail.empty())
        return SourceError("A2003: improper line syntax");
      SmallString<256> IncludedPath;
      ErrorOr<std::unique_ptr<MemoryBuffer>> IncludedBuffer = openIncludeFile(
          IncludedFilename, CurrentFilename, IncludeDirs, IncludedPath);
      if (!IncludedBuffer)
        return SourceError("unable to open include file '" + IncludedFilename +
                           "': " + IncludedBuffer.getError().message());
      EnsureDefaultSection();
      OS << ".incbin \"" << sys::path::convert_to_slash(IncludedPath) << '"';
    } else if (First.equals_insensitive("AREA")) {
      Literals.emit();
      SmallVector<StringRef, 8> Attributes;
      splitOperands(Tail, Attributes);
      if (Attributes.front().empty())
        return SourceError("A2003: improper line syntax");
      if (llvm::any_of(ArrayRef(Attributes).drop_front(),
                       [](StringRef Attribute) { return Attribute.empty(); }))
        return SourceError(
            "A2146: illegal symbol ,; AREA attribute expected");
      Expected<AreaName> Area = parseAreaName(Attributes.front());
      if (!Area)
        return SourceError(toString(Area.takeError()));
      bool IsCode = false;
      bool IsData = false;
      bool IsReadOnly = false;
      bool IsReadWrite = false;
      bool IsNoInit = false;
      bool IsPData = false;
      bool UsesCodeAlignment = false;
      unsigned Alignment = 3;
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
          if (!NoWarn && !IgnoredWarnings.contains(4039))
            WithColor::warning(DiagOS, ProgName)
                << CurrentFilename << ":" << CurrentLine
                << ": A4039: " << Attribute
                << " attribute does not pertain to a relocatable module; "
                   "ignored\n";
        } else if (Attribute.consume_front_insensitive("ALIGN=")) {
          Expected<uint64_t> Value = EvaluateAbsolute(Attribute);
          if (!Value)
            return SourceError(toString(Value.takeError()));
          if (*Value > 31)
            return SourceError("A2209: Immediate value " + Attribute +
                               " out of range");
          Alignment = *Value;
        } else if (Attribute.consume_front_insensitive("ASSOC=")) {
          Expected<AreaName> Associated = parseAreaName(Attribute);
          if (!Associated)
            return SourceError(toString(Associated.takeError()));
          AssociativeArea = *Associated;
        } else {
          return SourceError("A2041: unknown section flag: " + Attribute);
        }
      }

      if (((IsCode && (IsData || IsNoInit)) ||
           (IsReadOnly && IsReadWrite)) &&
          !NoWarn && !IgnoredWarnings.contains(4172))
        WithColor::warning(DiagOS, ProgName)
            << CurrentFilename << ":" << CurrentLine
            << ": A4172: illegal combination of section flags: section flags "
               "can not be inferred, code and data/uninitialized, "
               "readonly/readwrite\n";

      bool IsWritable =
          IsReadWrite || (!IsReadOnly && !IsCode && !IsPData);
      std::string Flags = IsCode     ? (IsWritable ? "xrw" : "xr")
                          : IsNoInit ? (IsWritable ? "bw" : "br")
                          : IsWritable ? "dw"
                                       : "dr";
      std::string SectionSuffix;
      raw_string_ostream SuffixOS(SectionSuffix);
      if (AssociativeArea) {
        if (AssociativeArea->ComdatSymbol.empty())
          return SourceError("A2003: improper line syntax");
        SuffixOS << ",associative,\"" << AssociativeArea->ComdatSymbol << '\"';
      } else if (!Area->ComdatSymbol.empty()) {
        SuffixOS << ",one_only,\"" << Area->ComdatSymbol << '\"';
      }
      SuffixOS.flush();
      std::string AreaKey =
          (Twine(Area->Name) + "{" + Area->ComdatSymbol + "}").str();
      bool IsNewArea = SeenAreas.insert(AreaKey).second;
      std::string AttributeSignature =
          (Twine(IsCode) + "," + Twine(IsReadOnly) + "," +
           Twine(IsReadWrite) + "," + Twine(IsNoInit) + "," +
           Twine(IsPData) + "," + Twine(UsesCodeAlignment) + "," +
           Twine(Alignment) + "," + SectionSuffix)
              .str();
      if (IsNewArea) {
        AreaAlignments[AreaKey] = 1ULL << Alignment;
        AreaCodeAlignments[AreaKey] = UsesCodeAlignment;
        AreaIsCode[AreaKey] = IsCode;
        AreaIsNoInit[AreaKey] = IsNoInit;
        AreaFlags[AreaKey] = Flags;
        AreaSectionSuffixes[AreaKey] = SectionSuffix;
        AreaAttributeSignatures[AreaKey] = AttributeSignature;
        AreaBaseSymbols[AreaKey] =
            (Twine(".Larmasm64_area_") + Twine(AreaBaseSymbolCount++)).str();
      } else {
        if (AreaAttributeSignatures.lookup(AreaKey) != AttributeSignature &&
            !NoWarn && !IgnoredWarnings.contains(4043))
          WithColor::warning(DiagOS, ProgName)
              << CurrentFilename << ":" << CurrentLine
              << ": A4043: redefinition of section flags ignored\n";
        IsCode = AreaIsCode.lookup(AreaKey);
        IsNoInit = AreaIsNoInit.lookup(AreaKey);
        Flags = AreaFlags.lookup(AreaKey);
        SectionSuffix = AreaSectionSuffixes.lookup(AreaKey);
      }
      CurrentAreaIsCode = IsCode;
      CurrentAreaIsNoInit = IsNoInit;
      CurrentAreaName = Area->Name.str();
      CurrentAreaAlignment = AreaAlignments.lookup(AreaKey);
      CurrentAreaUsesCodeAlignment = AreaCodeAlignments.lookup(AreaKey);
      CurrentAreaBaseSymbol = AreaBaseSymbols.lookup(AreaKey);
      OS << ".section \"" << Area->Name << "\",\"" << Flags << "\""
         << SectionSuffix;
      ++NumericLabelScope;
      NumericLabelRoutName.clear();
      DefinedNumericLabels.clear();
      if (IsNewArea)
        OS << "; .p2align " << Alignment;
      if (IsNewArea)
        OS << "; " << CurrentAreaBaseSymbol << ':';
      CurrentDebugCodeSection.reset();
      if (Debug.Enabled && IsCode) {
        auto Existing = DebugCodeSectionIndices.find(AreaKey);
        if (Existing == DebugCodeSectionIndices.end()) {
          unsigned Index = DebugCodeSections.size();
          unsigned FunctionId = Index;
          std::string EndSymbol =
              (Twine(".Larmasm64_debug_section_end_") + Twine(Index)).str();
          DebugCodeSections.push_back({Area->Name.str(), Flags, SectionSuffix,
                                       CurrentAreaBaseSymbol,
                                       std::move(EndSymbol), FunctionId});
          DebugCodeSectionIndices[AreaKey] = Index;
          CurrentDebugCodeSection = Index;
          OS << "; .cv_func_id " << FunctionId;
        } else {
          CurrentDebugCodeSection = Existing->second;
        }
      }
    } else if (isEquDirective(Second)) {
      StringRef Name = unquoteIdentifier(First);
      if (ConflictsWithObjectDefinition(Name))
        return SymbolConflict(Name);
      DefinedObjectSymbols.insert(Name);
      DefinedSymbols.insert(Name);
      std::string AssemblerName =
          getAssemblerSymbolName(Exports.contains(Name)
                                     ? Name
                                     : (".Larmasm$" + Name).str());
      Constants[Name] = AssemblerName;
      VariableExpressionParser Parser(AfterFirst, Variables, AbsoluteConstants,
                                      NoEscape, &DefinedSymbols,
                                      /*RegisterRelativeValues=*/nullptr,
                                      /*SymbolSizes=*/nullptr,
                                      &BuiltinVariables);
      Expected<VariableValue> Value = Parser.parse();
      if (Value && Value->Kind == VariableKind::Arithmetic)
        AbsoluteConstants[Name] = Value->Arithmetic;
      else if (!Value)
        consumeError(Value.takeError());
      OS << ".equ " << AssemblerName << ", "
         << rewriteSymbols(AfterFirst, Constants);
    } else if (Second.equals_insensitive("PROC") ||
               Second.equals_insensitive("FUNCTION")) {
      if (ActiveProcedureArea)
        return SourceError("A2092: improper program syntax; missing ENDP "
                           "directive or nested function definition");
      StringRef Name = unquoteIdentifier(First);
      if (ConflictsWithObjectDefinition(Name))
        return SymbolConflict(Name);
      EnsureDefaultSection();
      DefinedObjectSymbols.insert(Name);
      ActiveProcedureArea = CurrentAreaName;
      AtProcedureStart = true;
      std::string AssemblerName = getAssemblerSymbolName(Name);
      if (!Exports.contains(Name)) {
        OS << ".def " << AssemblerName << "; .scl 6; ";
        if (Debug.Enabled)
          OS << ".type 32; ";
        OS << ".endef; ";
      } else if (Debug.Enabled) {
        OS << ".def " << AssemblerName << "; .scl 2; .type 32; .endef; .globl "
           << AssemblerName << "; ";
      }
      OS << AssemblerName << ':';
      ActiveDebugProcedure.reset();
      if (Debug.Enabled && CurrentAreaIsCode &&
          DebugSymbolNames.insert(Name).second) {
        std::string EndSymbol =
            (Twine(".Larmasm64_debug_proc_end_") + Twine(DebugEndSymbolCount++))
                .str();
        ActiveDebugProcedure = DebugSymbols.size();
        DebugSymbols.push_back({Name.str(), std::move(AssemblerName),
                                std::move(EndSymbol),
                                /*IsProcedure=*/true,
                                /*IsExternal=*/Exports.contains(Name)});
      }
    } else if (First.equals_insensitive("ENDP") ||
               First.equals_insensitive("ENDFUNC") ||
               Second.equals_insensitive("ENDP") ||
               Second.equals_insensitive("ENDFUNC")) {
      StringRef EndTail = First.equals_insensitive("ENDP") ||
                                  First.equals_insensitive("ENDFUNC")
                              ? Tail
                              : AfterFirst;
      if (!EndTail.empty())
        return SourceError("A2003: improper line syntax: " + EndTail);
      if (!ActiveProcedureArea)
        return SourceError(
            "A2093: improper program syntax; unexpected ENDP directive");
      if (ActiveDebugProcedure)
        OS << "; " << DebugSymbols[*ActiveDebugProcedure].EndSymbol << ':';
      ActiveDebugProcedure.reset();
      ActiveProcedureArea.reset();
      AtProcedureStart = false;
    } else if (IsStorageLine) {
      EnsureDefaultSection();
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
        return SourceError("A2173: syntax error in expression");

      Expected<uint64_t> Count = EvaluateAbsolute(Operands[0]);
      if (!Count)
        return SourceError(toString(Count.takeError()));
      int64_t SignedCount = static_cast<int64_t>(*Count);
      if (SignedCount < 0)
        return SourceError("A2209: Immediate value " + Twine(SignedCount) +
                           " out of range");

      uint64_t Value = 0;
      uint64_t ValueSize = 1;
      if (Operands.size() >= 2) {
        Expected<uint64_t> EvaluatedValue = EvaluateAbsolute(Operands[1]);
        if (!EvaluatedValue)
          return SourceError(toString(EvaluatedValue.takeError()));
        Value = *EvaluatedValue;
      }
      if (Operands.size() == 3) {
        Expected<uint64_t> EvaluatedSize = EvaluateAbsolute(Operands[2]);
        if (!EvaluatedSize)
          return SourceError(toString(EvaluatedSize.takeError()));
        ValueSize = *EvaluatedSize;
      }
      if (ValueSize != 1 && ValueSize != 2 && ValueSize != 4)
        return SourceError(
            "A2197: value size for FILL directive must be 1, 2, or 4");
      if (*Count % ValueSize)
        return SourceError("A2219: Fill size must be a multiple of value size");
      uint64_t MaxValue =
          ValueSize == 4 ? UINT32_MAX : (1ULL << (ValueSize * 8)) - 1;
      if (Value > MaxValue)
        return SourceError("A2209: Immediate value " +
                           Twine(static_cast<int64_t>(Value)) +
                           " out of range");
      if (CurrentAreaIsNoInit && Value != 0)
        return SourceError("A2048: initialized data in an uninitialized data "
                           "section");

      if (HasLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (!NumericLabel) {
          if (ConflictsWithObjectDefinition(Name))
            return SymbolConflict(Name);
          DefinedObjectSymbols.insert(Name);
          std::string AssemblerName = getAssemblerSymbolName(Name);
          if (!Exports.contains(Name))
            OS << ".def " << AssemblerName << "; .scl 3; .endef; ";
          OS << AssemblerName << ":; ";
          RecordDebugLabel(Name);
        } else {
          EmitNumericLabel();
          OS << "; ";
        }
        if (!NumericLabel)
          SymbolSizes[Name] = *Count;
      }
      if (*Count)
        EmitDebugLocation();
      if (Value == 0)
        OS << ".space " << *Count;
      else
        OS << ".fill " << *Count / ValueSize << ", " << ValueSize << ", "
           << Value;
    } else if (IsDataLine) {
      EnsureDefaultSection();
      bool HasLabel = !isDataDirective(First);
      StringRef Directive = HasLabel ? Second : First;
      StringRef Values = HasLabel ? AfterFirst : Tail;
      bool IsByte = Directive.equals_insensitive("DCB") || Directive == "=";
      bool IsWord = Directive.equals_insensitive("DCW") ||
                    Directive.equals_insensitive("DCWU");
      bool IsLong = Directive.equals_insensitive("DCD") ||
                    Directive.equals_insensitive("DCDU") || Directive == "&" ||
                    Directive.equals_insensitive("DCI") ||
                    Directive.equals_insensitive("DCI.W");
      bool IsSingle = Directive.equals_insensitive("DCFS") ||
                      Directive.equals_insensitive("DCFSU");
      bool IsDouble = Directive.equals_insensitive("DCFD") ||
                      Directive.equals_insensitive("DCFDU");
      bool IsUnaligned = Directive.equals_insensitive("DCWU") ||
                         Directive.equals_insensitive("DCDU") ||
                         Directive.equals_insensitive("DCQU") ||
                         Directive.equals_insensitive("DCFSU") ||
                         Directive.equals_insensitive("DCFDU");
      unsigned Alignment = IsByte ? 1 : IsWord ? 2 : 4;
      unsigned DataSize = IsByte ? 1 : IsWord ? 2 : IsLong ? 4 : 8;
      if (!IsUnaligned && Alignment != 1)
        OS << ".balign " << Alignment << "; ";

      std::optional<std::string> DataLabel;
      if (HasLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (!NumericLabel) {
          if (ConflictsWithObjectDefinition(Name))
            return SymbolConflict(Name);
          DefinedObjectSymbols.insert(Name);
          std::string AssemblerName = getAssemblerSymbolName(Name);
          if (!Exports.contains(Name))
            OS << ".def " << AssemblerName << "; .scl 3; .endef; ";
          OS << AssemblerName << ":; ";
          RecordDebugLabel(Name);
        } else {
          EmitNumericLabel();
          OS << "; ";
        }
        if (!NumericLabel)
          DataLabel = Name.str();
      }
      if (UsesPC)
        EmitPCLabel();
      EmitDebugLocation();

      SmallVector<StringRef, 8> Operands;
      splitOperands(Values, Operands);
      if (Operands.empty() || llvm::any_of(Operands, [](StringRef Operand) {
            return Operand.empty();
          }))
        return SourceError("A2173: syntax error in expression");

      uint64_t EmittedSize = 0;
      for (auto [Index, Operand] : llvm::enumerate(Operands)) {
        if (Index)
          OS << "; ";
        auto EmitValue = [&](StringRef Prefix, const Twine &Expression,
                             bool IsSymbolic) {
          std::string ExpressionString = Expression.str();
          OS << Prefix;
          size_t ExpressionOffset = OS.tell();
          OS << ExpressionString;
          PreviousData =
              PreviousDataDefinition{ExpressionOffset, ExpressionString.size(),
                                     DataSize, ExpressionString, IsSymbolic,
                                     /*ReplaceWithCurrentLocation=*/false,
                                     /*IsInstruction=*/false};
        };

        if (IsSingle || IsDouble) {
          APFloat Float(IsSingle ? APFloat::IEEEsingle()
                                 : APFloat::IEEEdouble());
          APFloat::opStatus Status;
          Expected<VariableValue> Value = Evaluate(Operand);
          if (Value) {
            if (Value->Kind != VariableKind::Arithmetic)
              return SourceError(
                  "A2062: illegal expression type; expected absolute numeric");
            Status = Float.convertFromAPInt(APInt(64, Value->Arithmetic),
                                            /*IsSigned=*/true,
                                            APFloat::rmNearestTiesToEven);
          } else {
            consumeError(Value.takeError());
            Expected<APFloat::opStatus> Converted =
                Float.convertFromString(Operand, APFloat::rmNearestTiesToEven);
            if (!Converted) {
              consumeError(Converted.takeError());
              return SourceError("A2173: syntax error in expression");
            }
            Status = *Converted;
          }
          if (Status & APFloat::opOverflow)
            return SourceError(
                IsSingle
                    ? "A2022: Floating point value can not be represented in "
                      "single precision"
                    : "A2220: Floating point value out of range");
          EmitValue(IsSingle ? ".long " : ".quad ",
                    Twine(Float.bitcastToAPInt().getZExtValue()),
                    /*IsSymbolic=*/false);
          EmittedSize += DataSize;
          continue;
        }

        if (UsesPC && Operand.contains(PCSymbol)) {
          EmitValue(IsByte   ? ".byte "
                    : IsWord ? ".short "
                    : IsLong ? ".long "
                             : ".quad ",
                    normalizeSymbolicExpression(Operand, Constants),
                    /*IsSymbolic=*/true);
          EmittedSize += DataSize;
          continue;
        }

        Expected<VariableValue> Value = Evaluate(Operand);
        if (!Value) {
          std::string Message = toString(Value.takeError());
          if (!StringRef(Message).starts_with(
                  "unknown variable or constant '") &&
              !Operand.contains('|'))
            return SourceError(Message);
          if (IsByte)
            return SourceError("A2065: illegal expression type; expected "
                               "absolute numeric or string");
          if (IsWord)
            return SourceError(
                "A2061: illegal expression type; expected absolute numeric");
          EmitValue(IsByte   ? ".byte "
                    : IsWord ? ".short "
                    : IsLong ? ".long "
                             : ".quad ",
                    normalizeSymbolicExpression(Operand, Constants),
                    /*IsSymbolic=*/true);
          EmittedSize += DataSize;
          continue;
        }
        if (Value->Kind == VariableKind::String) {
          if (!IsByte)
            return SourceError(
                "A2062: illegal expression type; expected absolute numeric");
          if (!Value->String.empty()) {
            OS << ".byte ";
            for (auto [ByteIndex, Byte] : llvm::enumerate(Value->String)) {
              if (ByteIndex)
                OS << ", ";
              OS << static_cast<unsigned>(static_cast<unsigned char>(Byte));
            }
            PreviousData = PreviousDataDefinition{
                0, 0, static_cast<unsigned>(Value->String.size()),
                                                  /*Expression=*/{},
                                                  /*IsSymbolic=*/false,
                                                  /*ReplaceWithCurrentLocation=*/
                                                      false,
                                                  /*IsInstruction=*/false};
          }
          EmittedSize += Value->String.size();
          continue;
        }
        if (Value->Kind != VariableKind::Arithmetic)
          return SourceError(
              "A2062: illegal expression type; expected absolute numeric");

        int64_t Minimum = IsByte   ? -128
                          : IsWord ? -32768
                          : IsLong ? INT32_MIN
                                   : INT64_MIN;
        uint64_t Maximum = IsByte   ? UINT8_MAX
                           : IsWord ? UINT16_MAX
                           : IsLong ? UINT32_MAX
                                    : UINT64_MAX;
        if (!isIntegerValueInRange(Value->Arithmetic, Minimum, Maximum))
          return SourceError("A2209: Immediate value " +
                             Twine(static_cast<int64_t>(Value->Arithmetic)) +
                             " out of range");
        EmitValue(IsByte   ? ".byte "
                  : IsWord ? ".short "
                  : IsLong ? ".long "
                           : ".quad ",
                  static_cast<int64_t>(Value->Arithmetic) < 0
                      ? Twine(static_cast<int64_t>(Value->Arithmetic))
                      : Twine(Value->Arithmetic),
                  /*IsSymbolic=*/false);
        EmittedSize += DataSize;
      }
      if (DataLabel)
        SymbolSizes[*DataLabel] = EmittedSize;
      AtProcedureStart = false;
    } else if (First.equals_insensitive("ROUT") ||
               Second.equals_insensitive("ROUT")) {
      StringRef RoutTail = First.equals_insensitive("ROUT") ? Tail : AfterFirst;
      if (!RoutTail.empty())
        return SourceError("A2003: improper line syntax");
      if (Second.equals_insensitive("ROUT")) {
        EnsureDefaultSection();
        StringRef Name = unquoteIdentifier(First);
        if (ConflictsWithObjectDefinition(Name) ||
            DefinedObjectSymbols.contains(Name))
          return SymbolConflict(Name);
        DefinedObjectSymbols.insert(Name);
        std::string AssemblerName = getAssemblerSymbolName(Name);
        if (!Exports.contains(Name))
          OS << ".def " << AssemblerName << "; .scl 3; .endef; ";
        OS << ".set " << AssemblerName << ", " << CurrentAreaBaseSymbol;
        SymbolSizes[Name] = 0;
      }
      ++NumericLabelScope;
      NumericLabelRoutName =
          First.equals_insensitive("ROUT") ? std::string() : First.str();
      DefinedNumericLabels.clear();
    } else if (First.equals_insensitive("ALIGN")) {
      SmallVector<StringRef, 4> Operands;
      splitOperands(Tail, Operands);
      if (Operands.size() > 4 ||
          llvm::any_of(ArrayRef(Operands).drop_front(),
                       [](StringRef Operand) { return Operand.empty(); }))
        return SourceError("A2003: improper line syntax");

      uint64_t Alignment = 4;
      uint64_t Offset = 0;
      uint64_t Fill = 0;
      uint64_t FillSize = 1;
      bool HasFill = Operands.size() >= 3;
      if (!Operands.front().empty()) {
        Expected<uint64_t> Value = EvaluateAbsolute(Operands[0]);
        if (!Value)
          return SourceError(toString(Value.takeError()));
        Alignment = *Value;
      }
      if (!isPowerOf2_64(Alignment) || Alignment > (1ULL << 31))
        return SourceError("A2209: Immediate value " + Twine(Alignment) +
                           " out of range");
      if (Operands.size() >= 2) {
        Expected<uint64_t> Value = EvaluateAbsolute(Operands[1]);
        if (!Value)
          return SourceError(toString(Value.takeError()));
        Offset = *Value;
      }
      if (HasFill) {
        Expected<uint64_t> Value = EvaluateAbsolute(Operands[2]);
        if (!Value)
          return SourceError(toString(Value.takeError()));
        Fill = *Value;
        FillSize = CurrentAreaIsCode ? 4 : 1;
      } else if (CurrentAreaIsCode && CurrentAreaUsesCodeAlignment) {
        Fill = 0xd503201f;
        FillSize = 4;
      }
      if (Operands.size() == 4) {
        Expected<uint64_t> Value = EvaluateAbsolute(Operands[3]);
        if (!Value)
          return SourceError(toString(Value.takeError()));
        FillSize = *Value;
      }
      if (FillSize != 1 && FillSize != 2 && FillSize != 4)
        return SourceError("A2197: value size must be 1, 2, or 4");
      if (CurrentAreaBaseSymbol.empty())
        return SourceError("A2088: no current AREA");
      if (Alignment > CurrentAreaAlignment && !NoWarn &&
          !IgnoredWarnings.contains(4228))
        WithColor::warning(DiagOS, ProgName)
            << CurrentFilename << ":" << CurrentLine
            << ": A4228: Alignment value exceeds AREA alignment; alignment "
               "not guaranteed\n";

      std::string Here =
          (Twine(".Larmasm64_align_") + Twine(AlignSymbolCount++)).str();
      std::string Padding =
          (Twine("((") + Twine(Offset) + " - (" + Here + " - " +
           CurrentAreaBaseSymbol + ")) & (" + Twine(Alignment) + " - 1))")
              .str();
      OS << Here << ":\n";
      if (FillSize != 1)
        OS << ".if ((" << Padding << " % " << FillSize << ") != 0)\n"
           << ".error \"A2226: The given alignment pad doesn't evenly divide "
              "the number of padding bytes\"\n"
           << ".endif\n";
      OS << ".fill (" << Padding << " / " << FillSize << "), " << FillSize
         << ", " << Fill;
    } else if (First.equals_insensitive("ADRL") ||
               Second.equals_insensitive("ADRL")) {
      EnsureDefaultSection();
      bool HasLabel = Second.equals_insensitive("ADRL");
      StringRef OperandsText = HasLabel ? AfterFirst : Tail;
      SmallVector<StringRef, 2> Operands;
      splitOperands(OperandsText, Operands);
      if (Operands.size() != 2 || Operands[0].empty() || Operands[1].empty())
        return SourceError("A2003: improper line syntax");

      if (HasLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (ConflictsWithObjectDefinition(Name))
          return SymbolConflict(Name);
        DefinedObjectSymbols.insert(Name);
        std::string AssemblerName = getAssemblerSymbolName(Name);
        if (!Exports.contains(Name)) {
          OS << ".def " << AssemblerName << "; .scl 6; ";
          if (AtProcedureStart)
            OS << ".type 32; ";
          OS << ".endef; ";
        }
        OS << AssemblerName << ":; ";
        RecordDebugLabel(Name);
        SymbolSizes[Name] = 8;
      }

      std::string Target = normalizeSymbolicExpression(Operands[1], Constants);
      EmitDebugLocation();
      OS << "adrp " << Operands[0] << ", " << Target << "; add " << Operands[0]
         << ", " << Operands[0] << ", :lo12:" << Target;
      AtProcedureStart = false;
    } else if (First.equals_insensitive("B") ||
               First.equals_insensitive("BL") ||
               Second.equals_insensitive("B") ||
               Second.equals_insensitive("BL")) {
      EnsureDefaultSection();
      bool HasLabel = Second.equals_insensitive("B") ||
                      Second.equals_insensitive("BL");
      StringRef Mnemonic = HasLabel ? Second : First;
      StringRef TargetText = HasLabel ? AfterFirst : Tail;
      SmallVector<StringRef, 2> Operands;
      splitOperands(TargetText, Operands);
      if (Operands.size() != 1 || Operands[0].empty())
        return SourceError("A2003: improper line syntax");

      if (HasLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (ConflictsWithObjectDefinition(Name))
          return SymbolConflict(Name);
        DefinedObjectSymbols.insert(Name);
        std::string AssemblerName = getAssemblerSymbolName(Name);
        if (!Exports.contains(Name)) {
          OS << ".def " << AssemblerName << "; .scl 6; ";
          if (AtProcedureStart)
            OS << ".type 32; ";
          OS << ".endef; ";
        }
        OS << AssemblerName << ":; ";
        RecordDebugLabel(Name);
        SymbolSizes[Name] = 4;
      }

      std::string Target = normalizeSymbolicExpression(Operands[0], Constants);
      EmitDebugLocation();
      OS << Mnemonic << ' ';
      size_t ExpressionOffset = OS.tell();
      OS << Target;
      PreviousData = PreviousDataDefinition{
          ExpressionOffset, Target.size(), 4, Target,
          /*IsSymbolic=*/true, /*ReplaceWithCurrentLocation=*/true,
          /*IsInstruction=*/true};
      AtProcedureStart = false;
    } else if (First.equals_insensitive("KEEP")) {
      SmallVector<StringRef, 2> Operands;
      splitOperands(Tail, Operands);
      if (Operands.size() != 1 || Operands[0].empty())
        return SourceError("A2003: improper line syntax");
    } else if (First.equals_insensitive("END")) {
      if (ActiveProcedureArea)
        return SourceError("A2057: missing ENDP directive in section " +
                           *ActiveProcedureArea);
      Literals.emit();
    } else {
      if (!First.empty())
        EnsureDefaultSection();
      bool HasLabel =
          !Line.empty() && !isSpace(Line.front()) && !First.starts_with("#");
      if (HasLabel) {
        StringRef Name = unquoteIdentifier(First);
        if (!NumericLabel) {
          if (ConflictsWithObjectDefinition(Name))
            return SymbolConflict(Name);
          DefinedObjectSymbols.insert(Name);
          std::string AssemblerName = getAssemblerSymbolName(Name);
          if (!Exports.contains(Name)) {
            OS << ".def " << AssemblerName << "; .scl "
               << (CurrentAreaIsCode ? 6 : 3) << "; ";
            if (CurrentAreaIsCode && AtProcedureStart)
              OS << ".type 32; ";
            OS << ".endef; ";
          }
          OS << AssemblerName << ':';
          RecordDebugLabel(Name);
        } else {
          EmitNumericLabel();
        }
        if (!Second.empty()) {
          if (!NumericLabel)
            SymbolSizes[Name] = 4;
          OS << "; ";
          EmitDebugLocation();
          std::string Rewritten = rewriteSymbols(Tail, Constants);
          PendingSymbolUses.emplace_back(OS.tell(), Rewritten.size());
          OS << Rewritten;
        }
      } else {
        bool RewroteLiteralLoad = false;
        if (!First.empty())
          EmitDebugLocation();
        if (First.equals_insensitive("LDR")) {
          SmallVector<StringRef, 2> Operands;
          splitOperands(Tail, Operands);
          if (Operands.size() == 2) {
            StringRef Expression = Operands[1].trim();
            if (Expression.consume_front("=")) {
              Expression = Expression.trim();
              Expected<VariableValue> Value = Evaluate(Expression);
              if (Value) {
                if (Value->Kind != VariableKind::Arithmetic)
                  return SourceError(
                      "A2062: illegal expression type; expected absolute "
                      "numeric or program relative");
                StringRef Register = Operands[0].trim();
                bool IsWRegister = Register.starts_with_insensitive("w");
                bool IsXRegister = Register.starts_with_insensitive("x");
                unsigned Width = IsWRegister ? 32 : 64;
                int64_t SignedValue = static_cast<int64_t>(Value->Arithmetic);
                if (Width == 32 && Value->Arithmetic > UINT32_MAX &&
                    (SignedValue < INT32_MIN || SignedValue >= 0))
                  return SourceError("A2209: Immediate value " +
                                     Twine(SignedValue) + " out of range");
                bool UseMov = (IsWRegister || IsXRegister) &&
                              isSingleMovImmediate(Value->Arithmetic, Width);
                std::string LiteralExpression;
                raw_string_ostream ExpressionOS(LiteralExpression);
                if (SignedValue < 0)
                  ExpressionOS << SignedValue;
                else
                  ExpressionOS << Value->Arithmetic;
                ExpressionOS.flush();
                if (UseMov) {
                  OS << "mov " << Register << ", #" << LiteralExpression;
                } else if (IsWRegister || IsXRegister) {
                  OS << "ldr " << Register << ", "
                     << Literals.add(LiteralExpression, Width / 8);
                } else {
                  OS << "ldr " << Register << ", =" << LiteralExpression;
                }
                RewroteLiteralLoad = true;
              } else {
                std::string Message = toString(Value.takeError());
                if (!StringRef(Message).starts_with(
                        "unknown variable or constant '") &&
                    !Expression.contains('|'))
                  return SourceError(
                      "A2062: illegal expression type; expected absolute "
                      "numeric or program relative");
                StringRef Register = Operands[0].trim();
                std::string LiteralExpression =
                    normalizeSymbolicExpression(Expression, Constants);
                if (Register.starts_with_insensitive("w") ||
                    Register.starts_with_insensitive("x")) {
                  unsigned Size = Register.starts_with_insensitive("w") ? 4 : 8;
                  OS << "ldr " << Register << ", "
                     << Literals.add(LiteralExpression, Size);
                } else {
                  OS << "ldr " << Register << ", =" << LiteralExpression;
                }
                RewroteLiteralLoad = true;
              }
            }
          }
        }
        if (!RewroteLiteralLoad) {
          std::string Rewritten = rewriteSymbols(Line, Constants);
          PendingSymbolUses.emplace_back(OS.tell(), Rewritten.size());
          OS << Rewritten;
        }
      }
      if ((HasLabel && !Second.empty()) || (!HasLabel && !First.empty())) {
        PreviousData = PreviousDataDefinition{
            0, 0, 4, /*Expression=*/{}, /*IsSymbolic=*/false,
            /*ReplaceWithCurrentLocation=*/false,
            /*IsInstruction=*/true};
        AtProcedureStart = false;
      }
    }
    OS << '\n';
  }

  Literals.emit();

  if (ActiveProcedureArea)
    return createStringError(inconvertibleErrorCode(),
                             Twine(CurrentFilename) + ":" + Twine(CurrentLine) +
                                 ": A2057: missing ENDP directive in section " +
                                 *ActiveProcedureArea);

  for (const auto &Export : ExportLocations)
    if (!DefinedObjectSymbols.contains(Export.first()) &&
        !CommonSymbols.contains(Export.first())) {
      const auto &Location = Export.second;
      return createStringError(
          inconvertibleErrorCode(),
          Twine(Location.first) + ":" + Twine(Location.second) +
              ": A2023: undefined symbol: " + Export.first());
    }

  if (Debug.Enabled) {
    for (const DebugCodeSection &Section : DebugCodeSections)
      OS << ".section \"" << Section.Name << "\",\"" << Section.Flags << "\""
         << Section.Suffix << "; " << Section.EndSymbol << ":\n";

    OS << ".section \".debug$S\",\"dr\"; .p2align 2\n"
          ".long 4\n"
          ".long 241\n"
          ".long .Larmasm64_cv_compiler_end-.Larmasm64_cv_compiler_begin\n"
          ".Larmasm64_cv_compiler_begin:\n"
          ".short .Larmasm64_cv_obj_end-.Larmasm64_cv_obj_begin\n"
          ".Larmasm64_cv_obj_begin:\n"
          ".short 0x1101; .long 0; .asciz "
       << quoteAssemblyString(Debug.ObjectFilename)
       << "\n.Larmasm64_cv_obj_end:\n"
          ".short .Larmasm64_cv_compile_end-.Larmasm64_cv_compile_begin\n"
          ".Larmasm64_cv_compile_begin:\n"
          ".short 0x113c; .long 3; .short 0xf6\n"
          ".short 0; .short 0; .short 0; .short 0\n"
          ".short "
       << LLVM_VERSION_MAJOR << "; .short " << LLVM_VERSION_MINOR << "; .short "
       << LLVM_VERSION_PATCH << "; .short 0\n.asciz "
       << quoteAssemblyString("LLVM ARMASM64 Assembler " LLVM_VERSION_STRING)
       << "\n.Larmasm64_cv_compile_end:\n"
          ".Larmasm64_cv_compiler_end:\n"
          ".p2align 2\n";

    if (!Debug.SourceLink.empty())
      OS << ".long 241; .long 8\n"
            ".short 6; .short 0x117e; .long 0x1002\n"
            ".p2align 2\n";

    OS << ".cv_stringtable\n.cv_filechecksums\n";
    for (const DebugCodeSection &Section : DebugCodeSections)
      if (Section.HasLines)
        OS << ".cv_linetable " << Section.FunctionId << ", "
           << Section.StartSymbol << ", " << Section.EndSymbol << '\n';

    unsigned SymbolIndex = 0;
    for (const DebugSymbol &Symbol : DebugSymbols) {
      std::string Prefix =
          (Twine(".Larmasm64_cv_symbol_") + Twine(SymbolIndex++)).str();
      OS << ".long 241\n.long " << Prefix << "_sub_end-" << Prefix
         << "_sub_begin\n"
         << Prefix << "_sub_begin:\n.short " << Prefix << "_record_end-"
         << Prefix << "_record_begin\n"
         << Prefix << "_record_begin:\n";
      if (Symbol.IsProcedure) {
        OS << ".short " << (Symbol.IsExternal ? 0x1110 : 0x110f)
           << "\n.long 0; .long 0; .long 0\n.long " << Symbol.EndSymbol << '-'
           << Symbol.AssemblerName << "\n.long 0\n.long " << Symbol.EndSymbol
           << '-' << Symbol.AssemblerName << "\n.long 0x1001\n.secrel32 "
           << Symbol.AssemblerName << "\n.secidx " << Symbol.AssemblerName
           << "\n.byte 0\n.asciz " << quoteAssemblyString(Symbol.Name) << '\n';
      } else {
        OS << ".short 0x1105\n.secrel32 " << Symbol.AssemblerName
           << "\n.secidx " << Symbol.AssemblerName << "\n.byte 0\n.asciz "
           << quoteAssemblyString(Symbol.Name) << '\n';
      }
      OS << Prefix << "_record_end:\n";
      if (Symbol.IsProcedure)
        OS << ".short 2; .short 6\n";
      OS << Prefix << "_sub_end:\n.p2align 2\n";
    }

    OS << ".section \".debug$T\",\"dr\"; .p2align 2\n"
          ".long 4\n"
          ".short 6; .short 0x1201; .long 0\n"
          ".short 14; .short 0x1008; .long 3; .byte 0; .byte 0; .short 0; "
          ".long 0x1000\n";
    if (!Debug.SourceLink.empty())
      OS << ".short .Larmasm64_cv_sourcelink_end-"
            ".Larmasm64_cv_sourcelink_begin\n"
            ".Larmasm64_cv_sourcelink_begin:\n"
            ".short 0x1605; .long 0; .asciz "
         << quoteAssemblyString(Debug.SourceLink)
         << "\n.Larmasm64_cv_sourcelink_end:\n";
  }
  OS.flush();
  for (const PendingWeakExternal &Weak : PendingWeakExternals)
    if (!Weak.Conditional || containsIdentifier(Translated, Weak.Name))
      OS << ".armasm64_weak_external "
         << getAssemblerSymbolName(Weak.Name) << ", "
         << getAssemblerSymbolName(Weak.Fallback) << ", " << Weak.Search
         << '\n';

  OS.flush();
  for (const RelocatedExpression &Expression :
       llvm::reverse(RelocatedExpressions)) {
    std::string Replacement(Expression.Length, ' ');
    Replacement.front() = Expression.ReplaceWithCurrentLocation ? '.' : '0';
    Translated.replace(Expression.Offset, Expression.Length, Replacement);
  }
  for (size_t I = PendingSymbolUses.size(); I != 0; --I) {
    auto [Offset, Length] = PendingSymbolUses[I - 1];
    std::string Rewritten = rewriteRegisterAliases(
        StringRef(Translated).substr(Offset, Length), RegisterAliases);
    Rewritten = rewriteStorageMapFields(Rewritten, StorageMapFields);
    Translated.replace(Offset, Length, Rewritten);
  }

  return MemoryBuffer::getMemBufferCopy(Translated,
                                        Input->getBufferIdentifier());
}

static int assembleInput(StringRef ProgName, StringRef InputFilename,
                         StringRef OutputFilename, StringRef Machine,
                         const InputArgList &Args, raw_ostream &DiagOS,
                         StringRef CommandLine) {
  DenseSet<unsigned> IgnoredWarnings;
  for (StringRef ValueList : Args.getAllArgValues(OPT_ignore)) {
    SmallVector<StringRef, 4> Values;
    ValueList.split(Values, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
    for (StringRef Value : Values) {
      unsigned Number;
      if (Value.getAsInteger(10, Number)) {
        WithColor::error(DiagOS, ProgName) << "A2182: warning value expected\n";
        return 1;
      }
      IgnoredWarnings.insert(Number);
    }
  }

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

  std::vector<std::string> Predefines = Args.getAllArgValues(OPT_predefine);
  DebugOptions Debug;
  Debug.Enabled = Args.hasArg(OPT_debug_info);
  Debug.ObjectFilename = OutputFilename.str();
  if (Arg *Hash = Args.getLastArg(OPT_sha1, OPT_sha256))
    Debug.UseSHA1 = Hash->getOption().matches(OPT_sha1);
  if (Debug.Enabled)
    if (Arg *SourceLink = Args.getLastArg(OPT_source_link)) {
      ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
          MemoryBuffer::getFile(SourceLink->getValue(), /*IsText=*/true);
      if (!Buffer) {
        WithColor::error(DiagOS, ProgName)
            << "can't open file: " << SourceLink->getValue() << '\n';
        return 1;
      }
      Debug.SourceLink = (*Buffer)->getBuffer().trim().str();
    }
  Expected<std::unique_ptr<MemoryBuffer>> ExpandedInput = expandAssemblyControl(
      std::move(*InputOrErr), IncludeDirs, ProgName, Args.hasArg(OPT_no_warn),
      Args.hasArg(OPT_no_escape), IgnoredWarnings, DiagOS, Predefines,
      CommandLine);
  if (!ExpandedInput) {
    WithColor::error(DiagOS, ProgName)
        << toString(ExpandedInput.takeError()) << '\n';
    return 1;
  }

  Expected<std::unique_ptr<MemoryBuffer>> TranslatedInput =
      translateInput(std::move(*ExpandedInput), IncludeDirs, ProgName,
                     Args.hasArg(OPT_no_warn), Args.hasArg(OPT_no_escape),
                     IgnoredWarnings, DiagOS, Predefines, CommandLine, Debug);
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
  ARMAsm64AsmParserExtension ARMAsm64ParserExtension;
  ARMAsm64ParserExtension.Initialize(*Parser);
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

  SmallString<256> DefaultOutput = sys::path::filename(Positional.front());
  sys::path::replace_extension(DefaultOutput, "obj");
  StringRef Output = Args.getLastArgValue(
      OPT_output,
      Positional.size() == 2 ? Positional.back() : StringRef(DefaultOutput));

  std::string CommandLine;
  raw_string_ostream CommandLineOS(CommandLine);
  CommandLineOS << ProgName;
  for (StringRef Argument : ArrayRef(ExpandedArgv).drop_front()) {
    CommandLineOS << ' ';
    if (Argument.find_first_of(" \t") == StringRef::npos)
      CommandLineOS << Argument;
    else
      CommandLineOS << '"' << Argument << '"';
  }

  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeAArch64TargetMC();
  LLVMInitializeAArch64AsmParser();
  return assembleInput(ProgName, Positional.front(), Output, Machine, Args,
                       *DiagOS, CommandLine);
}
