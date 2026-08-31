//===-- llvm-asaxp.cpp - Alpha AXP compatible assembler ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Config/llvm-config.h"
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
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SourceMgr.h"
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

class AsAxpOptTable : public GenericOptTable {
public:
  AsAxpOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable,
                        /*IgnoreCase=*/true) {}
};

static void printHelp(StringRef ProgName, const AsAxpOptTable &Table) {
  std::string Usage = (ProgName + " [options] filename").str();
  Table.printHelp(outs(), Usage.c_str(), "LLVM Alpha AXP Assembler");
}

static void handleDiagnostic(const SMDiagnostic &Diagnostic, void *Context) {
  raw_ostream &OS = *static_cast<raw_ostream *>(Context);
  if (Diagnostic.getLoc().isValid())
    Diagnostic.getSourceMgr()->printIncludeStackForDiagnostic(
        Diagnostic.getLoc(), OS);
  Diagnostic.print(nullptr, OS);
}

static Expected<std::string> findClang(StringRef Argv0) {
  SmallString<256> Sibling(sys::path::parent_path(Argv0));
#ifdef _WIN32
  sys::path::append(Sibling, "clang.exe");
#else
  sys::path::append(Sibling, "clang");
#endif
  if (sys::fs::exists(Sibling))
    return Sibling.str().str();
  if (ErrorOr<std::string> Program = sys::findProgramByName("clang"))
    return *Program;
  return createStringError(inconvertibleErrorCode(),
                           "unable to find clang for preprocessing");
}

static Expected<std::unique_ptr<MemoryBuffer>>
readInput(StringRef ProgName, StringRef Argv0, StringRef InputFilename,
          const InputArgList &Args, raw_ostream &DiagOS) {
  bool AlreadyPreprocessed =
      sys::path::extension(InputFilename).equals_insensitive(".i");
  if (Args.hasArg(OPT_nopp) || AlreadyPreprocessed) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> Input =
        MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
    if (!Input)
      return createStringError(Input.getError(), "unable to read '%s'",
                               InputFilename.str().c_str());
    return std::move(*Input);
  }

  Expected<std::string> Clang = findClang(Argv0);
  if (!Clang)
    return Clang.takeError();

  SmallString<256> Temporary;
  if (std::error_code EC =
          sys::fs::createTemporaryFile("llvm-asaxp", "i", Temporary))
    return createStringError(EC, "unable to create preprocessor output");

  SmallVector<std::string, 24> OwnedArgs;
  OwnedArgs.push_back(*Clang);
  OwnedArgs.emplace_back("-E");
  OwnedArgs.emplace_back("-P");
  OwnedArgs.emplace_back("-x");
  OwnedArgs.emplace_back("assembler-with-cpp");
  for (StringRef Value : Args.getAllArgValues(OPT_define))
    OwnedArgs.push_back((Twine("-D") + Value).str());
  for (StringRef Value : Args.getAllArgValues(OPT_undefine))
    OwnedArgs.push_back((Twine("-U") + Value).str());
  for (StringRef Value : Args.getAllArgValues(OPT_include_path))
    OwnedArgs.push_back((Twine("-I") + Value).str());
  OwnedArgs.emplace_back("-o");
  OwnedArgs.push_back(Temporary.str().str());
  OwnedArgs.push_back(InputFilename.str());

  SmallVector<StringRef, 24> Command;
  for (const std::string &Argument : OwnedArgs)
    Command.push_back(Argument);
  std::string ErrorMessage;
  int Result = sys::ExecuteAndWait(*Clang, Command, std::nullopt, {}, 0, 0,
                                   &ErrorMessage);
  if (Result != 0) {
    sys::fs::remove(Temporary);
    if (!ErrorMessage.empty())
      WithColor::error(DiagOS, ProgName) << ErrorMessage << '\n';
    return createStringError(inconvertibleErrorCode(),
                             "preprocessing failed with exit code %d", Result);
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> Input =
      MemoryBuffer::getFile(Temporary, /*IsText=*/true);
  sys::fs::remove(Temporary);
  if (!Input)
    return createStringError(Input.getError(),
                             "unable to read preprocessor output");
  return std::move(*Input);
}

static int assembleInput(StringRef ProgName, StringRef Argv0,
                         StringRef InputFilename, StringRef OutputFilename,
                         bool TASO, const InputArgList &Args,
                         raw_ostream &DiagOS) {
  Expected<std::unique_ptr<MemoryBuffer>> Input =
      readInput(ProgName, Argv0, InputFilename, Args, DiagOS);
  if (!Input) {
    WithColor::error(DiagOS, ProgName) << toString(Input.takeError()) << '\n';
    return 1;
  }

  Triple TheTriple("alpha-pc-windows-msvc");
  std::string Error;
  const Target *TheTarget = TargetRegistry::lookupTarget(TheTriple, Error);
  if (!TheTarget) {
    WithColor::error(DiagOS, ProgName) << Error << '\n';
    return 1;
  }

  MCTargetOptions MCOptions;
  MCOptions.AssemblyLanguage = "asaxp";
  SourceMgr SrcMgr;
  SrcMgr.setDiagHandler(handleDiagnostic, &DiagOS);
  SrcMgr.AddNewSourceBuffer(std::move(*Input), SMLoc());
  SrcMgr.setVirtualFileSystem(vfs::getRealFileSystem());
  SrcMgr.setIncludeDirs(Args.getAllArgValues(OPT_include_path));

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TheTriple));
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TheTriple, MCOptions));
  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TheTriple, /*CPU=*/"generic", TASO ? "+taso" : ""));
  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  if (!MRI || !MAI || !STI || !MCII) {
    WithColor::error(DiagOS, ProgName)
        << "unable to create Alpha target information\n";
    return 1;
  }

  MCContext Ctx(TheTriple, *MAI, *MRI, *STI, &SrcMgr);
  std::unique_ptr<MCObjectFileInfo> MOFI(TheTarget->createMCObjectFileInfo(
      Ctx, /*PIC=*/false, /*LargeCodeModel=*/false));
  Ctx.setObjectFileInfo(MOFI.get());
  Ctx.setMainFileName(InputFilename);
  SmallString<128> CWD;
  if (!sys::fs::current_path(CWD))
    Ctx.setCompilationDir(CWD);
  if (Args.hasArg(OPT_debug_info, OPT_debug_lines))
    Ctx.setGenDwarfForAssembly(true);

  std::error_code EC;
  auto Out =
      std::make_unique<ToolOutputFile>(OutputFilename, EC, sys::fs::OF_None);
  if (EC) {
    WithColor::error(DiagOS, ProgName)
        << OutputFilename << ": " << EC.message() << '\n';
    return 1;
  }

  auto Buffered = std::make_unique<buffer_ostream>(Out->os());
  raw_pwrite_stream *OS = Buffered.get();

  std::unique_ptr<MCCodeEmitter> Emitter(
      TheTarget->createMCCodeEmitter(*MCII, Ctx));
  std::unique_ptr<MCAsmBackend> Backend(
      TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));
  std::unique_ptr<MCObjectWriter> Writer = Backend->createObjectWriter(*OS);
  std::unique_ptr<MCStreamer> Streamer(TheTarget->createMCObjectStreamer(
      TheTriple, Ctx, std::move(Backend), std::move(Writer),
      std::move(Emitter), *STI));
  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, Ctx, *Streamer, *MAI));
  std::unique_ptr<MCTargetAsmParser> TargetParser(
      TheTarget->createMCAsmParser(*STI, *Parser, *MCII));
  if (!TargetParser) {
    WithColor::error(DiagOS, ProgName)
        << "Alpha target does not support assembly parsing\n";
    return 1;
  }
  Parser->setTargetParser(*TargetParser);
  if (Parser->Run(/*NoInitialTextSection=*/false))
    return 1;

  Out->keep();
  return 0;
}

} // namespace

int llvm_asaxp_main(int Argc, char **Argv, const ToolContext &) {
  StringRef ProgName = sys::path::filename(Argv[0]);
  AsAxpOptTable Table;
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = Table.ParseArgs(ArrayRef(Argv + 1, Argv + Argc),
                                      MissingArgIndex, MissingArgCount);

  if (MissingArgCount) {
    WithColor::error(errs(), ProgName)
        << "missing argument to '" << Argv[MissingArgIndex + 1] << "'\n";
    return 1;
  }
  for (Arg *A : Args.filtered(OPT_UNKNOWN)) {
    WithColor::error(errs(), ProgName)
        << "unknown argument '" << A->getSpelling() << "'\n";
    return 1;
  }
  if (Args.hasArg(OPT_help)) {
    printHelp(ProgName, Table);
    return 0;
  }

  SmallVector<StringRef, 2> Inputs;
  for (Arg *A : Args.filtered(OPT_INPUT))
    Inputs.push_back(A->getValue());
  if (Inputs.empty()) {
    WithColor::error(errs(), ProgName) << "missing input source file\n";
    return 1;
  }
  if (Inputs.size() != 1) {
    WithColor::error(errs(), ProgName) << "too many input source files\n";
    return 1;
  }

  StringRef Machine = Args.getLastArgValue(OPT_machine, "alpha64");
  bool TASO;
  if (Machine.equals_insensitive("alpha") ||
      Machine.equals_insensitive("taso")) {
    TASO = true;
  } else if (Machine.equals_insensitive("alpha64")) {
    TASO = false;
  } else {
    WithColor::error(errs(), ProgName)
        << "invalid machine type '" << Machine << "'\n";
    return 1;
  }

  SmallString<256> DefaultOutput(sys::path::filename(Inputs.front()));
  sys::path::replace_extension(DefaultOutput, "obj");
  StringRef Output = Args.getLastArgValue(OPT_output, DefaultOutput);

  if (!Args.hasArg(OPT_nologo))
    outs() << "LLVM Alpha AXP Assembler " LLVM_VERSION_STRING << '\n';

  LLVMInitializeAlphaTargetInfo();
  LLVMInitializeAlphaTargetMC();
  return assembleInput(ProgName, Argv[0], Inputs.front(), Output, TASO, Args,
                       errs());
}
