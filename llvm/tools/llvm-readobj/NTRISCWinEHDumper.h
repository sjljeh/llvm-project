//===- NTRISCWinEHDumper.h - Alpha/MIPS/PowerPC NTUW printing *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_NTRISCWINEHDUMPER_H
#define LLVM_TOOLS_LLVM_READOBJ_NTRISCWINEHDUMPER_H

#include "llvm/Support/ScopedPrinter.h"
#include <system_error>

namespace llvm {
namespace object {
class COFFObjectFile;
class SymbolRef;
struct coff_section;
} // namespace object

namespace NTRISCWinEH {

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
  /// Prints the .pdata of an Alpha, MIPS or PowerPC object or image,
  /// whether NTUW or NT 4.0.
  void printData(const Context &Ctx);
  /// Tests whether an object or image uses NTUW.
  static bool usesNTUW(const object::COFFObjectFile &COFF);

private:
  ScopedPrinter &SW;
};

} // namespace NTRISCWinEH
} // namespace llvm

#endif
