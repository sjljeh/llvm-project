//===- llvm/MC/MCWinCOFFObjectWriter.h - Win COFF Object Writer -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINCOFFOBJECTWRITER_H
#define LLVM_MC_MCWINCOFFOBJECTWRITER_H

#include "llvm/MC/MCObjectWriter.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCContext;
class MCFixup;
class MCValue;
class raw_pwrite_stream;

class MCWinCOFFObjectTargetWriter : public MCObjectTargetWriter {
  virtual void anchor();

  const unsigned Machine;

protected:
  MCWinCOFFObjectTargetWriter(unsigned Machine_);

public:
  ~MCWinCOFFObjectTargetWriter() override = default;

  Triple::ObjectFormatType getFormat() const override { return Triple::COFF; }
  static bool classof(const MCObjectTargetWriter *W) {
    return W->getFormat() == Triple::COFF;
  }

  unsigned getMachine() const { return Machine; }
  virtual unsigned getRelocType(MCContext &Ctx, const MCValue &Target,
                                const MCFixup &Fixup, bool IsCrossSection,
                                const MCAsmBackend &MAB) const = 0;
  virtual bool recordRelocation(const MCFixup &) const { return true; }
};

class WinCOFFWriter;

class WinCOFFObjectWriter final : public MCObjectWriter {
  friend class WinCOFFWriter;

  std::unique_ptr<MCWinCOFFObjectTargetWriter> TargetObjectWriter;
  std::unique_ptr<WinCOFFWriter> ObjWriter, DwoWriter;
  bool IncrementalLinkerCompatible = false;

public:
  /// A procedure that armasm, under -g, describes with a function definition
  /// and .bf/.ef symbols that give the lines of its PROC and ENDP.
  struct ARMASMFunction {
    const MCSymbol *Symbol;
    const MCSymbol *End;
    unsigned BeginLine;
    unsigned EndLine;
  };

private:
  SmallVector<ARMASMFunction, 0> ARMASMFunctions;
  SmallVector<const MCSymbol *, 0> ARMASMCOMDATKeys;

public:
  WinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS);
  WinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                      raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);

  // MCObjectWriter interface implementation.
  void reset() override;
  void setAssembler(MCAssembler *Asm) override;
  void setIncrementalLinkerCompatible(bool Value) {
    IncrementalLinkerCompatible = Value;
  }
  void addARMASMFunction(const ARMASMFunction &Function) {
    ARMASMFunctions.push_back(Function);
  }
  /// Name a symbol that armasm writes with the COMDAT names, ahead of the
  /// other symbols: the name in braces of a COMDAT area, associative or not.
  void addARMASMCOMDATKey(const MCSymbol *Symbol) {
    ARMASMCOMDATKeys.push_back(Symbol);
  }
  void executePostLayoutBinding() override;
  bool isSymbolRefDifferenceFullyResolvedImpl(const MCSymbol &SymA,
                                              const MCFragment &FB, bool InSet,
                                              bool IsPCRel) const override;
  void recordRelocation(const MCFragment &F, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override;
  uint64_t writeObject() override;
  int getSectionNumber(const MCSection &Section) const;
};

/// Construct a new Win COFF writer instance.
///
/// \param MOTW - The target specific WinCOFF writer subclass.
/// \param OS - The stream to write to.
/// \returns The constructed object writer.
LLVM_ABI std::unique_ptr<MCObjectWriter>
createWinCOFFObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                          raw_pwrite_stream &OS);

LLVM_ABI std::unique_ptr<MCObjectWriter>
createWinCOFFDwoObjectWriter(std::unique_ptr<MCWinCOFFObjectTargetWriter> MOTW,
                             raw_pwrite_stream &OS, raw_pwrite_stream &DwoOS);

/// The bucket of \p Name in the symbol hash table of Microsoft armasm and
/// armasm64, which write symbols (and CodeView symbol records) in bucket
/// order: a PJW/ELF hash of the name's bytes, read as signed chars, modulo
/// 503 buckets.
LLVM_ABI unsigned getARMASMSymbolBucket(StringRef Name);
} // end namespace llvm

#endif // LLVM_MC_MCWINCOFFOBJECTWRITER_H
