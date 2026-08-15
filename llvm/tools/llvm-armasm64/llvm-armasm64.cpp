//===-- llvm-armasm64.cpp - ARMASM64-compatible assembler -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/Support/Path.h"
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

} // namespace

int llvm_armasm64_main(int Argc, char **Argv, const ToolContext &) {
  StringRef ProgName = sys::path::filename(Argv[0]);
  ARMAsm64OptTable T;
  unsigned MissingArgIndex, MissingArgCount;
  InputArgList Args = T.ParseArgs(ArrayRef(Argv + 1, Argc - 1), MissingArgIndex,
                                  MissingArgCount);

  if (Args.hasArg(OPT_help)) {
    std::string Usage =
        (ProgName + " [options] sourcefile [objectfile]").str();
    T.printHelp(outs(), Usage.c_str(), "LLVM ARMASM64 Assembler");
    return 0;
  }

  WithColor::error(errs(), ProgName)
      << (Args.hasArg(OPT_INPUT) ? "assembly is not implemented"
                                 : "missing input source file")
      << '\n';
  return 1;
}
