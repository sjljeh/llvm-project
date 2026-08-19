//===-- llvm-ms-mc.cpp - Microsoft-compatible Message Compiler -*- C++ -*-===//
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
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

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

static constexpr opt::OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

class MsMcOptTable : public opt::GenericOptTable {
public:
  MsMcOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable,
                        /*IgnoreCase=*/false) {}
};

struct NamedValue {
  std::string Name;
  uint32_t Value;
  std::string Symbol;
};

struct LanguageInfo {
  std::string Name;
  uint32_t Id;
  std::string Suffix;
};

struct MessageText {
  LanguageInfo Language;
  std::string Text;
};

struct Message {
  uint32_t Code = 0;
  uint32_t Severity = 0;
  uint32_t Facility = 0;
  std::string Symbol;
  std::vector<MessageText> Texts;
  std::vector<std::string> HeaderComments;
};

struct Block {
  uint32_t LowId;
  uint32_t HighId;
  SmallVector<char, 0> Entries;
};

[[noreturn]] static void reportError(const Twine &Message) {
  WithColor::error(errs(), "llvm-ms-mc") << Message << '\n';
  exit(1);
}

static void append16(SmallVectorImpl<char> &Output, uint16_t Value) {
  char Bytes[2];
  support::endian::write16le(Bytes, Value);
  Output.append(Bytes, Bytes + sizeof(Bytes));
}

static void append32(SmallVectorImpl<char> &Output, uint32_t Value) {
  char Bytes[4];
  support::endian::write32le(Bytes, Value);
  Output.append(Bytes, Bytes + sizeof(Bytes));
}

static UTF16 cp1252ToUnicode(uint8_t C) {
  static constexpr UTF16 Map80[] = {
      0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
      0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
      0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
      0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178,
  };
  if (C >= 0x80 && C <= 0x9f)
    return Map80[C - 0x80];
  return C;
}

static uint8_t unicodeToCp1252(UTF16 C) {
  if (C <= 0x7f || (C >= 0xa0 && C <= 0xff))
    return C;
  for (uint16_t Byte = 0x80; Byte <= 0x9f; ++Byte)
    if (cp1252ToUnicode(Byte) == C)
      return Byte;
  return '?';
}

static std::string encodeCp1252(StringRef Input) {
#if defined(_WIN32)
  SmallVector<wchar_t, 0> UTF16Text;
  if (std::error_code EC = sys::windows::UTF8ToUTF16(Input, UTF16Text))
    reportError("unable to encode generated text: " + EC.message());
  SmallVector<char, 0> Output;
  if (std::error_code EC = sys::windows::UTF16ToCurCP(UTF16Text.data(),
                                                      UTF16Text.size(), Output))
    reportError("unable to encode generated text: " + EC.message());
  return std::string(Output.begin(), Output.end());
#else
  SmallVector<UTF16, 0> UTF16Text;
  if (!convertUTF8ToUTF16String(Input, UTF16Text))
    reportError("unable to encode generated text");

  std::string Output;
  Output.reserve(UTF16Text.size());
  for (UTF16 C : UTF16Text) {
    switch (C) {
    case 0x0102:
    case 0x0103:
      Output.push_back(C == 0x0102 ? 'A' : 'a');
      break;
    case 0x0118:
    case 0x0119:
      Output.push_back(C == 0x0118 ? 'E' : 'e');
      break;
    case 0x0141:
    case 0x0142:
      Output.push_back(C == 0x0141 ? 'L' : 'l');
      break;
    default:
      Output.push_back(unicodeToCp1252(C));
    }
  }
  return Output;
#endif
}

static void writeFile(StringRef Path, StringRef Contents) {
  std::error_code EC;
  raw_fd_ostream OS(Path, EC, sys::fs::OF_None);
  if (EC)
    reportError(Path + ": " + EC.message());
  OS.write(Contents.data(), Contents.size());
}

static void writeTextFile(StringRef Path, StringRef Contents,
                          StringRef Encoding) {
  std::string Output;
  if (Encoding.equals_insensitive("utf-8")) {
    Output = "\xef\xbb\xbf";
    Output += Contents;
  } else if (Encoding.equals_insensitive("utf-16")) {
    SmallVector<UTF16, 0> UTF16Text;
    if (!convertUTF8ToUTF16String(Contents, UTF16Text))
      reportError("unable to encode generated text");
    SmallVector<char, 0> Bytes;
    append16(Bytes, UNI_UTF16_BYTE_ORDER_MARK_NATIVE);
    for (UTF16 C : UTF16Text)
      append16(Bytes, C);
    Output.assign(Bytes.begin(), Bytes.end());
  } else if (Encoding.equals_insensitive("ansi")) {
    Output = encodeCp1252(Contents);
  } else {
    reportError("unsupported output code page '" + Encoding + "'");
  }
  writeFile(Path, Output);
}

static std::string withCRLF(StringRef Input) {
  std::string Output;
  Output.reserve(Input.size() + Input.count('\n'));
  for (char C : Input) {
    if (C == '\n')
      Output.push_back('\r');
    Output.push_back(C);
  }
  return Output;
}

class MessageCompiler {
  bool Customer;
  bool Decimal;
  bool NullTerminate;
  bool UnicodeOutput;
  std::string Typedef;
  std::vector<NamedValue> FacilityDefinitions;
  std::vector<NamedValue> SeverityDefinitions;
  StringMap<uint32_t> Facilities;
  StringMap<uint32_t> Severities;
  StringMap<LanguageInfo> Languages;
  std::vector<LanguageInfo> LanguageOrder;
  std::vector<std::string> HeaderComments;
  std::vector<Message> Messages;
  bool HasLanguageDefinitions = false;
  uint32_t LastCode = 0;
  bool HasLastCode = false;

  uint32_t parseNumber(StringRef Value, size_t LineNumber) const {
    uint64_t Result;
    if (Value.getAsInteger(0, Result) || Result > UINT32_MAX)
      reportError("line " + Twine(LineNumber) + ": invalid integer '" + Value +
                  "'");
    return Result;
  }

  uint32_t parseMessageCode(StringRef Value, size_t LineNumber) {
    Value = Value.trim();
    if (Value.empty()) {
      if (!HasLastCode)
        return 1;
      return LastCode + 1;
    }
    if (Value.consume_front("+")) {
      if (!HasLastCode)
        reportError("line " + Twine(LineNumber) +
                    ": relative message ID has no predecessor");
      return LastCode + parseNumber(Value, LineNumber);
    }
    return parseNumber(Value, LineNumber);
  }

  uint32_t resolveValue(StringRef Name, const StringMap<uint32_t> &Values,
                        size_t LineNumber) const {
    auto It = Values.find(Name);
    if (It != Values.end())
      return It->second;
    uint64_t Result;
    if (!Name.getAsInteger(0, Result) && Result <= UINT32_MAX)
      return Result;
    reportError("line " + Twine(LineNumber) + ": unknown name '" + Name + "'");
  }

  std::string collectDefinitionList(ArrayRef<StringRef> Lines, size_t &Index,
                                    StringRef Line) const {
    size_t Open = Line.find('(');
    if (Open == StringRef::npos)
      reportError("line " + Twine(Index + 1) + ": expected '('");

    std::string Result;
    StringRef Rest = Line.drop_front(Open + 1);
    while (true) {
      size_t Comment = Rest.find(';');
      if (Comment != StringRef::npos)
        Rest = Rest.take_front(Comment);
      size_t Close = Rest.find(')');
      StringRef Part = Close == StringRef::npos ? Rest : Rest.take_front(Close);
      Result += Part.str();
      Result.push_back(' ');
      if (Close != StringRef::npos)
        return Result;
      if (++Index >= Lines.size())
        reportError("unterminated name list");
      Rest = Lines[Index].trim();
    }
  }

  void parseNamedValues(ArrayRef<StringRef> Lines, size_t &Index,
                        StringRef Line, StringMap<uint32_t> &Values,
                        std::vector<NamedValue> &Definitions) {
    std::string List = collectDefinitionList(Lines, Index, Line);
    SmallVector<StringRef, 16> Entries;
    SplitString(List, Entries);
    for (StringRef Entry : Entries) {
      auto [Name, Definition] = Entry.split('=');
      if (Definition.empty())
        reportError("line " + Twine(Index + 1) +
                    ": expected a name definition");
      auto [Number, Symbol] = Definition.split(':');
      uint32_t Value = parseNumber(Number, Index + 1);
      Values[Name] = Value;
      Definitions.push_back({Name.str(), Value, Symbol.str()});
    }
  }

  void parseLanguages(ArrayRef<StringRef> Lines, size_t &Index,
                      StringRef Line) {
    if (!HasLanguageDefinitions) {
      Languages.clear();
      LanguageOrder.clear();
      HasLanguageDefinitions = true;
    }

    std::string List = collectDefinitionList(Lines, Index, Line);
    SmallVector<StringRef, 16> Entries;
    SplitString(List, Entries);
    for (StringRef Entry : Entries) {
      auto [Name, Definition] = Entry.split('=');
      auto [Number, Suffix] = Definition.split(':');
      if (Name.empty() || Number.empty() || Suffix.empty())
        reportError("line " + Twine(Index + 1) +
                    ": invalid language definition");
      LanguageInfo Language{Name.str(), parseNumber(Number, Index + 1),
                            Suffix.str()};
      Languages[Name] = Language;
      LanguageOrder.push_back(std::move(Language));
    }
  }

  uint32_t messageId(const Message &M) const {
    return (M.Code & 0xffff) | ((M.Facility & 0xfff) << 16) |
           ((M.Severity & 3) << 30) | (Customer ? 0x20000000 : 0);
  }

  SmallVector<char, 0> encodeEntry(StringRef Text) const {
    SmallVector<char, 0> EncodedText;
    uint16_t Flags = UnicodeOutput ? 1 : 0;
    if (UnicodeOutput) {
      SmallVector<UTF16, 0> UTF16Text;
      if (!convertUTF8ToUTF16String(Text, UTF16Text))
        reportError("unable to encode message text");
      for (UTF16 C : UTF16Text)
        append16(EncodedText, C);
      if (NullTerminate)
        append16(EncodedText, 0);
    } else {
      std::string Ansi = encodeCp1252(Text);
      EncodedText.append(Ansi.begin(), Ansi.end());
      if (NullTerminate)
        EncodedText.push_back(0);
    }

    size_t TerminatorSize = UnicodeOutput ? 2 : 1;
    size_t Length = alignTo(4 + EncodedText.size() + TerminatorSize, 4);
    if (Length > UINT16_MAX)
      reportError("message text is too long");
    SmallVector<char, 0> Entry;
    append16(Entry, Length);
    append16(Entry, Flags);
    Entry.append(EncodedText);
    Entry.resize(Length, 0);
    return Entry;
  }

  std::vector<Block> makeBlocks(
      const std::map<uint32_t, const MessageText *> &LanguageMessages) const {
    std::vector<Block> Blocks;
    for (const auto &[Id, Text] : LanguageMessages) {
      if (Blocks.empty() || Id != Blocks.back().HighId + 1)
        Blocks.push_back({Id, Id, {}});
      else
        Blocks.back().HighId = Id;
      SmallVector<char, 0> Entry = encodeEntry(Text->Text);
      Blocks.back().Entries.append(Entry);
    }
    return Blocks;
  }

public:
  MessageCompiler(bool Customer, bool Decimal, bool NullTerminate,
                  bool UnicodeOutput)
      : Customer(Customer), Decimal(Decimal), NullTerminate(NullTerminate),
        UnicodeOutput(UnicodeOutput) {
    Facilities["System"] = 0;
    Facilities["Application"] = 0;
    Severities["Success"] = 0;
    Severities["Informational"] = 1;
    Severities["Warning"] = 2;
    Severities["Error"] = 3;
    LanguageInfo English{"English", 0x409, "MSG00001"};
    Languages["English"] = English;
    LanguageOrder.push_back(std::move(English));
  }

  void parse(StringRef Input) {
    SmallVector<StringRef, 0> Lines;
    Input.split(Lines, '\n', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
    for (StringRef &Line : Lines)
      Line.consume_back("\r");

    std::optional<Message> Current;
    for (size_t Index = 0; Index < Lines.size(); ++Index) {
      StringRef Line = Lines[Index];
      StringRef Trimmed = Line.trim();
      if (Trimmed.empty())
        continue;
      if (Trimmed.starts_with(";")) {
        size_t Semicolon = Line.find(';');
        std::vector<std::string> &Comments =
            Current ? Current->HeaderComments : HeaderComments;
        Comments.push_back(Line.drop_front(Semicolon + 1).str());
        continue;
      }

      auto [Key, Value] = Trimmed.split('=');
      Key = Key.trim();
      Value = Value.trim();
      if (Key == "MessageIdTypedef") {
        Typedef = Value.str();
      } else if (Key == "SeverityNames") {
        parseNamedValues(Lines, Index, Trimmed, Severities,
                         SeverityDefinitions);
      } else if (Key == "FacilityNames") {
        parseNamedValues(Lines, Index, Trimmed, Facilities,
                         FacilityDefinitions);
      } else if (Key == "LanguageNames") {
        parseLanguages(Lines, Index, Trimmed);
      } else if (Key == "MessageId") {
        if (Current)
          Messages.push_back(std::move(*Current));
        Current.emplace();
        Current->Code = parseMessageCode(Value, Index + 1);
        LastCode = Current->Code;
        HasLastCode = true;
      } else if (Key == "Severity") {
        if (!Current)
          reportError("line " + Twine(Index + 1) +
                      ": severity outside a message");
        Current->Severity = resolveValue(Value, Severities, Index + 1);
      } else if (Key == "Facility") {
        if (!Current)
          reportError("line " + Twine(Index + 1) +
                      ": facility outside a message");
        Current->Facility = resolveValue(Value, Facilities, Index + 1);
      } else if (Key == "SymbolicName") {
        if (!Current)
          reportError("line " + Twine(Index + 1) +
                      ": symbolic name outside a message");
        Current->Symbol = Value.str();
      } else if (Key == "Language") {
        if (!Current)
          reportError("line " + Twine(Index + 1) +
                      ": language outside a message");
        auto Language = Languages.find(Value);
        if (Language == Languages.end())
          reportError("line " + Twine(Index + 1) + ": unknown language '" +
                      Value + "'");
        std::string Text;
        bool Terminated = false;
        while (++Index < Lines.size()) {
          if (Lines[Index].trim() == ".") {
            Terminated = true;
            break;
          }
          Text += Lines[Index];
          Text += "\r\n";
        }
        if (!Terminated)
          reportError("unterminated message text");
        Current->Texts.push_back({Language->second, std::move(Text)});
      } else {
        reportError("line " + Twine(Index + 1) + ": unknown directive '" + Key +
                    "'");
      }
    }
    if (Current)
      Messages.push_back(std::move(*Current));
    if (Messages.empty())
      reportError("input contains no messages");
  }

  void writeHeader(StringRef Path, StringRef Encoding) const {
    std::string Header;
    raw_string_ostream OS(Header);
    auto WriteComment = [&](StringRef Comment) {
      if (Encoding.equals_insensitive("ansi")) {
        SmallVector<UTF16, 0> UTF16Text;
        if (!convertUTF8ToUTF16String(Comment, UTF16Text))
          reportError("unable to encode generated comment");
        auto End =
            std::find_if(UTF16Text.begin(), UTF16Text.end(), [](UTF16 C) {
              return unicodeToCp1252(C) == '?' && C != '?';
            });
        if (End != UTF16Text.end()) {
          std::string Prefix;
          if (!convertUTF16ToUTF8String(
                  ArrayRef<UTF16>(UTF16Text.data(), End - UTF16Text.begin()),
                  Prefix))
            reportError("unable to encode generated comment");
          OS << Prefix;
          return;
        }
      }
      OS << Comment << '\n';
    };
    for (StringRef Comment : HeaderComments)
      WriteComment(Comment);
    OS << "//\n"
          "//  Values are 32 bit values laid out as follows:\n"
          "//\n"
          "//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1\n"
          "//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 "
          "0\n"
          "//  "
          "+---+-+-+-----------------------+-------------------------------+\n"
          "//  |Sev|C|R|     Facility          |               Code            "
          "|\n"
          "//  "
          "+---+-+-+-----------------------+-------------------------------+\n"
          "//\n"
          "//  where\n"
          "//\n"
          "//      Sev - is the severity code\n"
          "//\n"
          "//          00 - Success\n"
          "//          01 - Informational\n"
          "//          10 - Warning\n"
          "//          11 - Error\n"
          "//\n"
          "//      C - is the Customer code flag\n"
          "//\n"
          "//      R - is a reserved bit\n"
          "//\n"
          "//      Facility - is the facility code\n"
          "//\n"
          "//      Code - is the facility's status code\n"
          "//\n"
          "//\n"
          "// Define the facility codes\n"
          "//\n";

    auto WriteDefinitions = [&](ArrayRef<NamedValue> Definitions) {
      for (const NamedValue &Definition : Definitions) {
        if (Definition.Symbol.empty())
          continue;
        std::string Value = Decimal ? utostr(Definition.Value)
                                    : "0x" + utohexstr(Definition.Value);
        OS << "#define "
           << left_justify(Definition.Symbol,
                           std::max<size_t>(33, Definition.Symbol.size() + 1))
           << Value << '\n';
      }
    };
    WriteDefinitions(FacilityDefinitions);
    OS << "\n\n//\n// Define the severity codes\n//\n";
    WriteDefinitions(SeverityDefinitions);
    OS << "\n\n";

    for (const Message &M : Messages) {
      OS << "//\n// MessageId: ";
      if (M.Symbol.empty())
        OS << "0x" << utohexstr(messageId(M), false, 8)
           << "L (No symbolic name defined)";
      else
        OS << M.Symbol;
      OS << "\n//\n// MessageText:\n//\n";
      if (!M.Texts.empty()) {
        SmallVector<StringRef, 8> TextLines;
        StringRef(M.Texts.front().Text)
            .split(TextLines, "\r\n", /*MaxSplit=*/-1, /*KeepEmpty=*/true);
        if (!TextLines.empty() && TextLines.back().empty())
          TextLines.pop_back();
        for (StringRef TextLine : TextLines)
          OS << "// " << TextLine << '\n';
      }
      OS << "//\n";
      if (M.Symbol.empty()) {
        OS << "\n\n";
      } else {
        OS << "#define "
           << left_justify(M.Symbol, std::max<size_t>(33, M.Symbol.size() + 1));
        std::string Id = "0x" + utohexstr(messageId(M), false, 8) + "L";
        if (Typedef.empty())
          OS << Id;
        else
          OS << "((" << Typedef << ')' << Id << ')';
        OS << "\n\n";
      }
      for (StringRef Comment : M.HeaderComments)
        WriteComment(Comment);
    }

    OS.flush();
    writeTextFile(Path, withCRLF(Header), Encoding);
  }

  void writeResources(StringRef Directory, StringRef Basename,
                      bool IncludeBasename, StringRef Encoding) const {
    std::string RC;
    raw_string_ostream RCStream(RC);
    for (const LanguageInfo &Language : LanguageOrder) {
      std::map<uint32_t, const MessageText *> LanguageMessages;
      for (const Message &M : Messages)
        for (const MessageText &Text : M.Texts)
          if (Text.Language.Id == Language.Id &&
              Text.Language.Suffix == Language.Suffix)
            LanguageMessages[messageId(M)] = &Text;
      if (LanguageMessages.empty())
        continue;

      std::vector<Block> Blocks = makeBlocks(LanguageMessages);
      SmallVector<char, 0> Binary;
      append32(Binary, Blocks.size());
      uint32_t Offset = 4 + Blocks.size() * 12;
      for (const Block &B : Blocks) {
        append32(Binary, B.LowId);
        append32(Binary, B.HighId);
        append32(Binary, Offset);
        Offset += B.Entries.size();
      }
      for (const Block &B : Blocks)
        Binary.append(B.Entries);

      std::string BinName;
      if (IncludeBasename)
        BinName = (Basename + "_" + Language.Suffix + ".bin").str();
      else
        BinName = Language.Suffix + ".bin";
      SmallString<256> BinPath(Directory);
      sys::path::append(BinPath, BinName);
      writeFile(BinPath, StringRef(Binary.data(), Binary.size()));

      uint32_t PrimaryLanguage = Language.Id & 0x3ff;
      uint32_t SubLanguage = Language.Id >> 10;
      RCStream << "LANGUAGE 0x" << utohexstr(PrimaryLanguage, true) << ",0x"
               << utohexstr(SubLanguage, true) << "\n1 11 \"" << BinName
               << "\"\n";
    }
    RCStream.flush();

    SmallString<256> RCPath(Directory);
    sys::path::append(RCPath, Basename + ".rc");
    writeTextFile(RCPath, withCRLF(RC), Encoding);
  }
};

static std::string decodeInput(StringRef Bytes, bool UnicodeInput) {
  if (Bytes.starts_with("\xef\xbb\xbf"))
    return Bytes.drop_front(3).str();

  bool HasUTF16LEBOM = Bytes.size() >= 2 &&
                       static_cast<uint8_t>(Bytes[0]) == 0xff &&
                       static_cast<uint8_t>(Bytes[1]) == 0xfe;
  if (UnicodeInput || HasUTF16LEBOM) {
    size_t Offset = HasUTF16LEBOM ? 2 : 0;
    if ((Bytes.size() - Offset) % 2)
      reportError("UTF-16 input has an odd byte count");
    SmallVector<UTF16, 0> UTF16Text;
    for (size_t I = Offset; I < Bytes.size(); I += 2)
      UTF16Text.push_back(
          support::endian::read16le(Bytes.data() + static_cast<ptrdiff_t>(I)));
    std::string UTF8Text;
    if (!convertUTF16ToUTF8String(UTF16Text, UTF8Text))
      reportError("unable to decode UTF-16 input");
    return UTF8Text;
  }

  SmallVector<UTF16, 0> UTF16Text;
  UTF16Text.reserve(Bytes.size());
  for (uint8_t C : Bytes.bytes())
    UTF16Text.push_back(cp1252ToUnicode(C));
  std::string UTF8Text;
  if (!convertUTF16ToUTF8String(UTF16Text, UTF8Text))
    reportError("unable to decode input");
  return UTF8Text;
}

} // namespace

int llvm_ms_mc_main(int Argc, char **Argv, const ToolContext &) {
  MsMcOptTable Table;
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = Table.ParseArgs(ArrayRef(Argv + 1, Argc - 1),
                                      MissingArgIndex, MissingArgCount);
  if (MissingArgCount)
    reportError("missing argument to '" +
                StringRef(Args.getArgString(MissingArgIndex)) + "'");
  if (Arg *A = Args.getLastArg(OPT_UNKNOWN))
    reportError("unknown argument '" + A->getAsString(Args) + "'");
  if (Args.hasArg(OPT_help)) {
    Table.printHelp(outs(), "llvm-ms-mc [options] <input.mc>",
                    "Microsoft-compatible Message Compiler");
    return 0;
  }

  std::vector<std::string> Inputs = Args.getAllArgValues(OPT_INPUT);
  if (Inputs.size() != 1)
    reportError(Inputs.empty() ? "no input file specified"
                               : "more than one input file specified");
  StringRef InputPath = Inputs.front();
  ErrorOr<std::unique_ptr<MemoryBuffer>> InputBuffer =
      MemoryBuffer::getFile(InputPath, /*IsText=*/false);
  if (!InputBuffer)
    reportError(InputPath + ": " + InputBuffer.getError().message());

  bool UnicodeInput = Args.hasArg(OPT_input_unicode);
  bool UnicodeOutput = !Args.hasArg(OPT_output_ansi);
  if (Args.hasArg(OPT_output_utf8))
    reportError("UTF-8 message tables are not yet supported");
  std::string Input = decodeInput((*InputBuffer)->getBuffer(), UnicodeInput);

  MessageCompiler Compiler(Args.hasArg(OPT_customer), Args.hasArg(OPT_decimal),
                           Args.hasArg(OPT_null_terminate), UnicodeOutput);
  Compiler.parse(Input);

  StringRef HeaderDirectory = Args.getLastArgValue(OPT_header_dir, ".");
  StringRef ResourceDirectory = Args.getLastArgValue(OPT_resource_dir, ".");
  SmallString<128> DefaultBasename(sys::path::stem(InputPath));
  StringRef Basename = Args.getLastArgValue(OPT_basename, DefaultBasename);
  StringRef Extension = Args.getLastArgValue(OPT_header_extension, "h");
  StringRef Encoding = Args.getLastArgValue(OPT_codepage, "ansi");

  SmallString<256> HeaderPath(HeaderDirectory);
  sys::path::append(HeaderPath, Basename + "." + Extension);
  Compiler.writeHeader(HeaderPath, Encoding);
  Compiler.writeResources(ResourceDirectory, Basename,
                          Args.hasArg(OPT_binary_name), Encoding);

  if (Args.hasArg(OPT_verbose))
    outs() << "MC: Compiling " << InputPath << '\n';
  return 0;
}
