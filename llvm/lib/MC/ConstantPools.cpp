//===- ConstantPools.cpp - ConstantPool class -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ConstantPool and  AssemblerConstantPools classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/ConstantPools.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

//
// ConstantPool implementation
//
// Emit the contents of the constant pool using the provided streamer.
void ConstantPool::emitEntries(MCStreamer &Streamer, bool AtSectionEnd) {
  if (Entries.empty())
    return;
  if (ARMASMLayout) {
    // armasm aligns a pool to 4 bytes, or to 8 when it places a pool with
    // 8-byte entries at the end of its section. The padding is relative to
    // the section start; the section keeps the alignment its AREA gave it.
    bool AlignTo8 = AtSectionEnd && llvm::any_of(Entries, [](const auto &E) {
                      return E.Size == 8;
                    });
    MCSection *Section = Streamer.getCurrentSectionOnly();
    Align SectionAlign = Section->getAlign();
    Streamer.emitValueToAlignment(Align(AlignTo8 ? 8 : 4), 0, 1, 0);
    Section->setAlignment(SectionAlign);
    for (const ConstantPoolEntry &Entry : llvm::reverse(Entries))
      if (Entry.Size == 8) {
        Streamer.emitLabel(Entry.Label);
        Streamer.emitValue(Entry.Value, Entry.Size, Entry.Loc);
      }
    for (const ConstantPoolEntry &Entry : Entries)
      if (Entry.Size != 8) {
        Streamer.emitLabel(Entry.Label);
        Streamer.emitValue(Entry.Value, Entry.Size, Entry.Loc);
      }
    Entries.clear();
    clearCache();
    return;
  }
  Streamer.emitDataRegion(MCDR_DataRegion);
  for (const ConstantPoolEntry &Entry : Entries) {
    Streamer.emitValueToAlignment(Align(Entry.Size)); // align naturally
    Streamer.emitLabel(Entry.Label);
    Streamer.emitValue(Entry.Value, Entry.Size, Entry.Loc);
  }
  Streamer.emitDataRegion(MCDR_DataRegionEnd);
  Entries.clear();
}

const MCExpr *ConstantPool::addEntry(const MCExpr *Value, MCContext &Context,
                                     unsigned Size, SMLoc Loc) {
  if (ARMASMLayout) {
    std::string Key;
    raw_string_ostream KeyOS(Key);
    KeyOS << Size << ':';
    Context.getAsmInfo().printExpr(KeyOS, *Value);
    auto [It, Inserted] = CachedTextEntries.try_emplace(Key, nullptr);
    if (!Inserted)
      return It->second;
    MCSymbol *CPEntryLabel = Context.createTempSymbol();
    Entries.push_back(ConstantPoolEntry(CPEntryLabel, Value, Size, Loc));
    It->second = MCSymbolRefExpr::create(CPEntryLabel, Context, Loc);
    return It->second;
  }

  const MCConstantExpr *C = dyn_cast<MCConstantExpr>(Value);
  const MCSymbolRefExpr *S = dyn_cast<MCSymbolRefExpr>(Value);

  // Check if there is existing entry for the same constant. If so, reuse it.
  if (C) {
    auto CItr = CachedConstantEntries.find(std::make_pair(C->getValue(), Size));
    if (CItr != CachedConstantEntries.end())
      return CItr->second;
  }

  // Check if there is existing entry for the same symbol. If so, reuse it.
  if (S) {
    auto SItr =
        CachedSymbolEntries.find(std::make_pair(&(S->getSymbol()), Size));
    if (SItr != CachedSymbolEntries.end())
      return SItr->second;
  }

  MCSymbol *CPEntryLabel = Context.createTempSymbol();

  Entries.push_back(ConstantPoolEntry(CPEntryLabel, Value, Size, Loc));
  const auto SymRef = MCSymbolRefExpr::create(CPEntryLabel, Context, Loc);
  if (C)
    CachedConstantEntries[std::make_pair(C->getValue(), Size)] = SymRef;
  if (S)
    CachedSymbolEntries[std::make_pair(&(S->getSymbol()), Size)] = SymRef;
  return SymRef;
}

bool ConstantPool::empty() { return Entries.empty(); }

void ConstantPool::clearCache() {
  CachedConstantEntries.clear();
  CachedSymbolEntries.clear();
  CachedTextEntries.clear();
}

//
// AssemblerConstantPools implementation
//
ConstantPool *AssemblerConstantPools::getConstantPool(MCSection *Section) {
  ConstantPoolMapTy::iterator CP = ConstantPools.find(Section);
  if (CP == ConstantPools.end())
    return nullptr;

  return &CP->second;
}

ConstantPool &
AssemblerConstantPools::getOrCreateConstantPool(MCSection *Section) {
  auto [It, Inserted] = ConstantPools.try_emplace(Section);
  if (Inserted)
    It->second.setARMASMLayout(ARMASMLayout);
  return It->second;
}

static void emitConstantPool(MCStreamer &Streamer, MCSection *Section,
                             ConstantPool &CP, bool AtSectionEnd = false) {
  if (!CP.empty()) {
    Streamer.switchSection(Section);
    CP.emitEntries(Streamer, AtSectionEnd);
  }
}

void AssemblerConstantPools::emitAll(MCStreamer &Streamer) {
  // Dump contents of assembler constant pools.
  for (auto &CPI : ConstantPools) {
    MCSection *Section = CPI.first;
    ConstantPool &CP = CPI.second;

    emitConstantPool(Streamer, Section, CP);
  }
}

void AssemblerConstantPools::emitForCurrentSection(MCStreamer &Streamer,
                                                   bool AtSectionEnd) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  if (ConstantPool *CP = getConstantPool(Section))
    emitConstantPool(Streamer, Section, *CP, AtSectionEnd);
}

bool AssemblerConstantPools::hasEntriesForCurrentSection(MCStreamer &Streamer) {
  ConstantPool *CP = getConstantPool(Streamer.getCurrentSectionOnly());
  return CP && !CP->empty();
}

void AssemblerConstantPools::clearCacheForCurrentSection(MCStreamer &Streamer) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  if (ConstantPool *CP = getConstantPool(Section))
    CP->clearCache();
}

const MCExpr *AssemblerConstantPools::addEntry(MCStreamer &Streamer,
                                               const MCExpr *Expr,
                                               unsigned Size, SMLoc Loc) {
  MCSection *Section = Streamer.getCurrentSectionOnly();
  return getOrCreateConstantPool(Section).addEntry(Expr, Streamer.getContext(),
                                                   Size, Loc);
}
