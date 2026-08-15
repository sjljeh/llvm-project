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

static bool expandResponseFiles(int Argc, char **Argv,
                                SmallVectorImpl<const char *> &ExpandedArgv,
                                StringSaver &Saver) {
  ExpandedArgv.append(Argv, Argv + Argc);
  for (unsigned Depth = 0; Depth != 20; ++Depth) {
    if (!cl::ExpandResponseFiles(Saver, cl::TokenizeWindowsCommandLine,
                                 ExpandedArgv))
      return false;

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
      return true;
    ExpandedArgv.assign(RewrittenArgv.begin(), RewrittenArgv.end());
  }

  return false;
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

static std::string translateString(StringRef String) {
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
    } else {
      OS << C;
    }
  }
  OS << '"';
  return Translated;
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

static std::unique_ptr<MemoryBuffer>
translateInput(std::unique_ptr<MemoryBuffer> Input) {
  StringRef Remaining = Input->getBuffer();
  std::string Translated;
  raw_string_ostream OS(Translated);
  StringMap<std::string> Constants;
  StringSet<> Exports;
  collectExports(Remaining, Exports);

  while (!Remaining.empty()) {
    auto [Line, Rest] = Remaining.split('\n');
    Remaining = Rest;
    Line.consume_back("\r");

    Line = stripComment(Line);
    StringRef Statement = Line.trim();
    StringRef Tail = Statement;
    StringRef First = takeToken(Tail);
    StringRef AfterFirst = Tail;
    StringRef Second = takeToken(AfterFirst);

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
                     Operand.back() == '"')
            OS << ".ascii " << translateString(Operand);
          else {
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
                         const InputArgList &Args) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> InputOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (std::error_code EC = InputOrErr.getError()) {
    WithColor::error(errs(), ProgName)
        << InputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  Triple TheTriple(Machine.equals_insensitive("ARM64EC")
                       ? "arm64ec-pc-windows-msvc"
                       : "aarch64-pc-windows-msvc");
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
  if (!TheTarget) {
    WithColor::error(errs(), ProgName) << Error << '\n';
    return 1;
  }

  MCTargetOptions MCOptions;
  MCOptions.AssemblyLanguage = "armasm64";
  MCOptions.MCNoWarn = Args.hasArg(OPT_no_warn);

  SourceMgr SrcMgr;
  SrcMgr.AddNewSourceBuffer(translateInput(std::move(*InputOrErr)), SMLoc());
  std::vector<std::string> IncludeDirs;
  for (StringRef Paths : Args.getAllArgValues(OPT_include_path)) {
    SmallVector<StringRef, 4> SplitPaths;
    Paths.split(SplitPaths, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    for (StringRef Path : SplitPaths)
      IncludeDirs.push_back(Path.str());
  }
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
    WithColor::error(errs(), ProgName)
        << "unable to create AArch64 target information\n";
    return 1;
  }

  MCContext Ctx(TheTriple, *MAI, *MRI, *STI, &SrcMgr);
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
    WithColor::error(errs(), ProgName)
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
    WithColor::error(errs(), ProgName)
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
  if (!expandResponseFiles(Argc, Argv, ExpandedArgv, Saver)) {
    WithColor::error(errs(), ProgName) << "unable to read response file\n";
    return 1;
  }

  ARMAsm64OptTable T;
  unsigned MissingArgIndex, MissingArgCount;
  ArrayRef<const char *> ArgvRef(ExpandedArgv);
  InputArgList Args =
      T.ParseArgs(ArgvRef.drop_front(), MissingArgIndex, MissingArgCount);

  if (MissingArgCount) {
    WithColor::error(errs(), ProgName)
        << "missing argument to '" << ArgvRef[MissingArgIndex + 1] << "'\n";
    return 1;
  }

  for (Arg *A : Args.filtered(OPT_UNKNOWN)) {
    StringRef Spelling = A->getSpelling();
    std::string Nearest;
    WithColor::error(errs(), ProgName)
        << "unknown argument '" << Spelling << "'";
    if (T.findNearest(Spelling, Nearest) < 2)
      errs() << "; did you mean '" << Nearest << "'?";
    errs() << '\n';
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
    WithColor::error(errs(), ProgName) << "missing input source file\n";
    return 1;
  }
  if (Positional.size() > 2 ||
      (Positional.size() == 2 && Args.hasArg(OPT_output))) {
    WithColor::error(errs(), ProgName) << "too many positional arguments\n";
    return 1;
  }

  StringRef Machine = Args.getLastArgValue(OPT_machine, "ARM64");
  if (!Machine.equals_insensitive("ARM64") &&
      !Machine.equals_insensitive("ARM64EC")) {
    WithColor::error(errs(), ProgName)
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
  return assembleInput(ProgName, Positional.front(), Output, Machine, Args);
}
