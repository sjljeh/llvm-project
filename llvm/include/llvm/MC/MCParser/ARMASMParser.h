//===- ARMASMParser.h - Parser for ARMASM-dialect assembly ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The ARMASM dialect is the assembly language accepted by Microsoft armasm and
// armasm64 (and, historically, the ARM Development Suite assembler). It is a
// line-oriented, two-pass language with column-1 labels, textual $variable
// substitution, MACRO/IF/WHILE assembly control and AREA-based sections.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCPARSER_ARMASMPARSER_H
#define LLVM_MC_MCPARSER_ARMASMPARSER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/Support/Compiler.h"
#include <string>
#include <vector>

namespace llvm {

class MCAsmInfo;
class MCAsmParser;
class MCContext;
class MCStreamer;
class SourceMgr;

struct ARMASMParserOptions {
  /// Directories searched by GET/INCLUDE/INCBIN after the including file's
  /// directory and the current directory.
  std::vector<std::string> IncludeDirs;

  /// SETA/SETL/SETS directives executed before the first source line, as
  /// given by -predefine.
  std::vector<std::string> Predefines;

  /// Treat backslashes in string literals literally (-noesc).
  bool NoEscape = false;

  /// The value of the {COMMANDLINE} built-in variable.
  std::string CommandLine;

  /// Merge consecutive conditional Thumb instructions into one IT block, as
  /// pre-ARMv8 assemblers did (-oldit). By default every conditional
  /// instruction gets its own IT instruction.
  bool LegacyITBlocks = false;

  /// Suppress all warnings (-nowarn), or only the listed ARMASM warning
  /// numbers (-ignore).
  bool NoWarnings = false;
  DenseSet<unsigned> IgnoredWarnings;

  /// CodeView debug information (-g, -gh:SHA1, -sourcelink:).
  bool DebugInfo = false;
  bool DebugUseSHA1 = false;
  std::string DebugObjectFilename;
  std::string DebugSourceLink;
};

/// Create an MCAsmParser instance for parsing ARMASM-style assembly, the
/// dialect accepted by Microsoft armasm and armasm64.
LLVM_ABI MCAsmParser *createMCARMASMParser(SourceMgr &, MCContext &,
                                           MCStreamer &, const MCAsmInfo &,
                                           const ARMASMParserOptions &);

} // end namespace llvm

#endif // LLVM_MC_MCPARSER_ARMASMPARSER_H
