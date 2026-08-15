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
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/StringSaver.h"
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
  (void)Output;

  WithColor::error(errs(), ProgName) << "assembly is not implemented\n";
  return 1;
}
