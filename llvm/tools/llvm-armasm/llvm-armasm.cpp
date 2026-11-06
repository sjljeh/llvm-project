//===-- llvm-armasm.cpp - ARMASM-compatible assembler driver ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A drop-in replacement for Microsoft armasm (32-bit ARM, Thumb-2) and
// armasm64 (AArch64). The program name selects the default: a name ending in
// "64" (llvm-armasm64, armasm64) assembles AArch64, anything else Thumb-2.
// The source language is handled by MC's ARMASM parser; this file only deals
// with the command line, diagnostics and object output.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseSet.h"
#include "llvm/Config/Targets.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/ARMASMParser.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Driver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;
using namespace llvm::opt;

namespace {

enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "Opts.inc"
#undef OPTION
};

#define OPTTABLE_CODE
#include "Opts.inc"

class ARMASMOptTable : public OptTable {
public:
  ARMASMOptTable() : OptTable(optionTables(), /*IgnoreCase=*/true) {}
};

struct DiagnosticContext {
  raw_ostream &OS;
  StringRef ProgName;
};

/// Print a diagnostic the way armasm does, so that build systems that parse
/// Microsoft tool output (MSBuild, Visual Studio) understand it:
///
///   file(line) : error A2034: unknown opcode: foo
///       source line
///     at file(line)          (one per enclosing macro expansion)
///         invocation line
void printDiagnostic(const SMDiagnostic &Diagnostic, void *Context) {
  auto &Diag = *static_cast<DiagnosticContext *>(Context);
  raw_ostream &OS = Diag.OS;
  StringRef Filename = Diagnostic.getFilename();
  int Line = Diagnostic.getLineNo();
  StringRef Message = Diagnostic.getMessage();

  // Notes without a message name an enclosing macro expansion.
  if (Diagnostic.getKind() == SourceMgr::DK_Note && Message.empty()) {
    OS << "  at " << Filename;
    if (Line > 0)
      OS << '(' << Line << ')';
    OS << '\n';
    if (!Diagnostic.getLineContents().empty())
      OS << "  " << Diagnostic.getLineContents() << '\n';
    return;
  }

  StringRef Kind = Diagnostic.getKind() == SourceMgr::DK_Error     ? "error"
                   : Diagnostic.getKind() == SourceMgr::DK_Warning ? "warning"
                                                                   : "note";
  if (Filename.empty() || Filename == "<unknown>") {
    (Diagnostic.getKind() == SourceMgr::DK_Error
         ? WithColor::error(OS, Diag.ProgName)
         : WithColor::warning(OS, Diag.ProgName))
        << Message << '\n';
    return;
  }

  OS << Filename;
  if (Line > 0)
    OS << '(' << Line << ')';
  OS << " : " << Kind;
  // Messages carrying an ARMASM diagnostic number read "A2034: ...".
  if (Message.size() > 6 && Message[0] == 'A' &&
      llvm::all_of(Message.substr(1, 4), isDigit) && Message[5] == ':')
    OS << ' ' << Message << '\n';
  else
    OS << ": " << Message << '\n';
  if (!Diagnostic.getLineContents().empty())
    OS << Diagnostic.getLineContents() << '\n';
}

void printHelp(StringRef ProgName, const ARMASMOptTable &T, bool Is64Bit) {
  std::string Usage = (ProgName + " [options] sourcefile [objectfile]").str();
  T.printHelp(outs(), Usage.c_str(),
              Is64Bit ? "LLVM ARMASM64 Assembler" : "LLVM ARMASM Assembler");
}

bool selectDiagnosticOutput(StringRef ProgName, const InputArgList &Args,
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

/// Expand @file response files and armasm's -via <file>.
Error expandResponseFiles(int Argc, char **Argv,
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

int assembleInput(StringRef ProgName, StringRef InputFilename,
                  StringRef OutputFilename, const Triple &TheTriple,
                  const InputArgList &Args, raw_ostream &DiagOS,
                  StringRef CommandLine) {
  bool Is64Bit = TheTriple.isAArch64();
  ARMASMParserOptions ParserOptions;
  for (StringRef ValueList : Args.getAllArgValues(OPT_ignore)) {
    SmallVector<StringRef, 4> Values;
    ValueList.split(Values, ',', /*MaxSplit=*/-1, /*KeepEmpty=*/true);
    for (StringRef Value : Values) {
      unsigned Number;
      if (Value.getAsInteger(10, Number)) {
        WithColor::error(DiagOS, ProgName) << "A2182: warning value expected\n";
        return 1;
      }
      ParserOptions.IgnoredWarnings.insert(Number);
    }
  }
  ParserOptions.NoWarnings = Args.hasArg(OPT_no_warn);
  ParserOptions.LegacyITBlocks = Args.hasArg(OPT_old_it);
  ParserOptions.NoEscape = Args.hasArg(OPT_no_escape);
  ParserOptions.CommandLine = CommandLine.str();
  ParserOptions.Predefines = Args.getAllArgValues(OPT_predefine);
  for (StringRef Paths : Args.getAllArgValues(OPT_include_path)) {
    SmallVector<StringRef, 4> SplitPaths;
    Paths.split(SplitPaths, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    for (StringRef Path : SplitPaths)
      ParserOptions.IncludeDirs.push_back(Path.str());
  }
  ParserOptions.DebugInfo = Args.hasArg(OPT_debug_info);
  ParserOptions.DebugObjectFilename = OutputFilename.str();
  if (Arg *Hash = Args.getLastArg(OPT_sha1, OPT_sha256))
    ParserOptions.DebugUseSHA1 = Hash->getOption().matches(OPT_sha1);
  if (ParserOptions.DebugInfo)
    if (Arg *SourceLink = Args.getLastArg(OPT_source_link)) {
      ErrorOr<std::unique_ptr<MemoryBuffer>> Buffer =
          MemoryBuffer::getFile(SourceLink->getValue(), /*IsText=*/true);
      if (!Buffer) {
        WithColor::error(DiagOS, ProgName)
            << "can't open file: " << SourceLink->getValue() << '\n';
        return 1;
      }
      ParserOptions.DebugSourceLink = (*Buffer)->getBuffer().trim().str();
    }

  ErrorOr<std::unique_ptr<MemoryBuffer>> InputOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (std::error_code EC = InputOrErr.getError()) {
    WithColor::error(DiagOS, ProgName)
        << InputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
  if (!TheTarget) {
    WithColor::error(DiagOS, ProgName) << Error << '\n';
    return 1;
  }

  MCTargetOptions MCOptions;
  MCOptions.AssemblyLanguage = Is64Bit ? "armasm64" : "armasm";
  MCOptions.MCNoWarn = ParserOptions.NoWarnings;

  DiagnosticContext DiagContext{DiagOS, ProgName};
  SourceMgr SrcMgr;
  SrcMgr.setDiagHandler(printDiagnostic, &DiagContext);
  SrcMgr.AddNewSourceBuffer(std::move(*InputOrErr), SMLoc());

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TheTriple));
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TheTriple, MCOptions));
  // Microsoft armasm64 accepts these extensions without architecture flags.
  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TheTriple, /*CPU=*/"",
      Is64Bit ? "+fullfp16,+dotprod,+sve2" : ""));
  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  if (!MRI || !MAI || !STI || !MCII) {
    WithColor::error(DiagOS, ProgName) << "unable to create target information\n";
    return 1;
  }

  MCContext Ctx(TheTriple, *MAI, *MRI, *STI, &SrcMgr);
  Ctx.setDiagnosticHandler([&DiagContext](const SMDiagnostic &Diagnostic, bool,
                                          const SourceMgr &,
                                          std::vector<const MDNode *> &) {
    printDiagnostic(Diagnostic, &DiagContext);
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

  std::unique_ptr<MCAsmParser> Parser(createMCARMASMParser(
      SrcMgr, Ctx, *Streamer, *MAI, ParserOptions));
  std::unique_ptr<MCTargetAsmParser> TargetParser(
      TheTarget->createMCAsmParser(*STI, *Parser, *MCII));
  if (!TargetParser) {
    WithColor::error(DiagOS, ProgName)
        << "target does not support assembly parsing\n";
    return 1;
  }
  Parser->setTargetParser(*TargetParser);

  if (Parser->Run(/*NoInitialTextSection=*/true))
    return 1;

  Out->keep();
  return 0;
}

} // namespace

int llvm_armasm_main(int Argc, char **Argv, const ToolContext &) {
  StringRef ProgName = sys::path::filename(Argv[0]);
  bool Is64Bit = sys::path::stem(ProgName).ends_with_insensitive("64");

  BumpPtrAllocator Alloc;
  StringSaver Saver(Alloc);
  SmallVector<const char *, 20> ExpandedArgv;
  ARMASMOptTable T;
  Error ExpandError = expandResponseFiles(Argc, Argv, ExpandedArgv, Saver);

  unsigned MissingArgIndex, MissingArgCount;
  ArrayRef<const char *> ArgvRef(ExpandedArgv);
  InputArgList Args =
      T.ParseArgs(ArgvRef.drop_front(), MissingArgIndex, MissingArgCount);

  raw_ostream *DiagOS;
  std::unique_ptr<raw_fd_ostream> DiagFile;
  if (!selectDiagnosticOutput(ProgName, Args, DiagFile, DiagOS)) {
    consumeError(std::move(ExpandError));
    return 1;
  }
  if (ExpandError) {
    WithColor::error(*DiagOS, ProgName) << toString(std::move(ExpandError))
                                        << '\n';
    return 1;
  }

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

  // -machine overrides the mode implied by the program name.
  StringRef Machine = Args.getLastArgValue(OPT_machine);
  if (!Machine.empty()) {
    if (Machine.equals_insensitive("ARM64") ||
        Machine.equals_insensitive("ARM64EC"))
      Is64Bit = true;
    else if (Machine.equals_insensitive("ARM") ||
             Machine.equals_insensitive("THUMB") ||
             Machine.equals_insensitive("ARMCE"))
      Is64Bit = false;
    else {
      WithColor::error(*DiagOS, ProgName)
          << "invalid machine type '" << Machine << "'\n";
      return 1;
    }
  }

  if (Args.hasArg(OPT_help)) {
    printHelp(ProgName, T, Is64Bit);
    return 0;
  }

  if (!Is64Bit && Args.hasArg(OPT_arm_mode)) {
    WithColor::error(*DiagOS, ProgName)
        << "-32 (ARM instruction set) is not supported; only Thumb-2 code "
           "can be assembled for Windows on ARM\n";
    return 1;
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

#if LLVM_HAS_AARCH64_TARGET
  LLVMInitializeAArch64TargetInfo();
  LLVMInitializeAArch64TargetMC();
  LLVMInitializeAArch64AsmParser();
#endif
#if LLVM_HAS_ARM_TARGET
  LLVMInitializeARMTargetInfo();
  LLVMInitializeARMTargetMC();
  LLVMInitializeARMAsmParser();
#endif
  Triple TheTriple(!Is64Bit ? "thumbv7-pc-windows-msvc"
                   : Machine.equals_insensitive("ARM64EC")
                       ? "arm64ec-pc-windows-msvc"
                       : "aarch64-pc-windows-msvc");
  return assembleInput(ProgName, Positional.front(), Output, TheTriple, Args,
                       *DiagOS, CommandLine);
}
