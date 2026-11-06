//===- ConstantPools.h - Keep track of assembler-generated ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ConstantPool and AssemblerConstantPools classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_CONSTANTPOOLS_H
#define LLVM_MC_CONSTANTPOOLS_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/SMLoc.h"
#include <cstdint>

namespace llvm {

class MCContext;
class MCExpr;
class MCSection;
class MCStreamer;
class MCSymbol;
class MCSymbolRefExpr;

struct ConstantPoolEntry {
  ConstantPoolEntry(MCSymbol *L, const MCExpr *Val, unsigned Sz, SMLoc Loc_)
    : Label(L), Value(Val), Size(Sz), Loc(Loc_) {}

  MCSymbol *Label;
  const MCExpr *Value;
  unsigned Size;
  SMLoc Loc;
};

// A class to keep track of assembler-generated constant pools that are use to
// implement the ldr-pseudo.
class ConstantPool {
  using EntryVecTy = SmallVector<ConstantPoolEntry, 4>;
  EntryVecTy Entries;

  // Caches of entries that already exist, indexed by their contents
  // and also the size of the constant.
  DenseMap<std::pair<int64_t, unsigned>, const MCSymbolRefExpr *>
      CachedConstantEntries;
  DenseMap<std::pair<const MCSymbol *, unsigned>, const MCSymbolRefExpr *>
      CachedSymbolEntries;

  // The layout used by ARMASM (armasm/armasm64): the pool is aligned once,
  // 8-byte entries come first in reverse order of use followed by 4-byte
  // entries in order of use, entries are shared by their textual form, and
  // each emitted pool starts a fresh set of entries.
  bool ARMASMLayout = false;
  StringMap<const MCSymbolRefExpr *> CachedTextEntries;

public:
  // Initialize a new empty constant pool
  ConstantPool() = default;

  void setARMASMLayout(bool V) { ARMASMLayout = V; }

  // Add a new entry to the constant pool in the next slot.
  // \param Value is the new entry to put in the constant pool.
  // \param Size is the size in bytes of the entry
  //
  // \returns a MCExpr that references the newly inserted value
  LLVM_ABI const MCExpr *addEntry(const MCExpr *Value, MCContext &Context,
                                  unsigned Size, SMLoc Loc);

  // Emit the contents of the constant pool using the provided streamer.
  LLVM_ABI void emitEntries(MCStreamer &Streamer);

  // Return true if the constant pool is empty
  LLVM_ABI bool empty();

  LLVM_ABI void clearCache();
};

class AssemblerConstantPools {
  // Map type used to keep track of per-Section constant pools used by the
  // ldr-pseudo opcode. The map associates a section to its constant pool. The
  // constant pool is a vector of (label, value) pairs. When the ldr
  // pseudo is parsed we insert a new (label, value) pair into the constant pool
  // for the current section and add MCSymbolRefExpr to the new label as
  // an opcode to the ldr. After we have parsed all the user input we
  // output the (label, value) pairs in each constant pool at the end of the
  // section.
  //
  // We use the MapVector for the map type to ensure stable iteration of
  // the sections at the end of the parse. We need to iterate over the
  // sections in a stable order to ensure that we have print the
  // constant pools in a deterministic order when printing an assembly
  // file.
  using ConstantPoolMapTy = MapVector<MCSection *, ConstantPool>;
  ConstantPoolMapTy ConstantPools;
  bool ARMASMLayout = false;

public:
  /// Use the ARMASM constant pool layout for pools created from now on.
  void setARMASMLayout(bool V) { ARMASMLayout = V; }
  LLVM_ABI void emitAll(MCStreamer &Streamer);
  LLVM_ABI void emitForCurrentSection(MCStreamer &Streamer);
  LLVM_ABI void clearCacheForCurrentSection(MCStreamer &Streamer);
  LLVM_ABI const MCExpr *addEntry(MCStreamer &Streamer, const MCExpr *Expr,
                                  unsigned Size, SMLoc Loc);

private:
  ConstantPool *getConstantPool(MCSection *Section);
  ConstantPool &getOrCreateConstantPool(MCSection *Section);
};

} // end namespace llvm

#endif // LLVM_MC_CONSTANTPOOLS_H
