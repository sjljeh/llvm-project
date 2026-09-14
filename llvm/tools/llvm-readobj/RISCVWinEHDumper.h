//===- RISCVWinEHDumper.h - RISC-V64 RVUW printing ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_RISCVWINEHDUMPER_H
#define LLVM_TOOLS_LLVM_READOBJ_RISCVWINEHDUMPER_H

#include "llvm/Support/ScopedPrinter.h"
#include <system_error>

namespace llvm {
namespace object {
class COFFObjectFile;
class SymbolRef;
struct coff_section;
} // namespace object

namespace RISCVWinEH {

class Dumper {
public:
  using SymbolResolver = std::error_code (*)(const object::coff_section *,
                                              uint64_t, object::SymbolRef &,
                                              void *);

  struct Context {
    const object::COFFObjectFile &COFF;
    SymbolResolver ResolveSymbol;
    void *UserData;

    Context(const object::COFFObjectFile &COFF, SymbolResolver Resolver,
            void *UserData)
        : COFF(COFF), ResolveSymbol(Resolver), UserData(UserData) {}
  };

  explicit Dumper(ScopedPrinter &SW) : SW(SW) {}
  void printData(const Context &Ctx);

private:
  ScopedPrinter &SW;
};

} // namespace RISCVWinEH
} // namespace llvm

#endif
