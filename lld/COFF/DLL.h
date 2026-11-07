//===- DLL.h ----------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_DLL_H
#define LLD_COFF_DLL_H

#include "Chunks.h"
#include "Symbols.h"
#include "InputFiles.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"

namespace lld::coff {

// Windows-specific.
// IdataContents creates all chunks for the DLL import table.
// You are supposed to call add() to add symbols and then
// call create() to populate the chunk vectors.
// Like link.exe, order imports by the first reference to them in the object
// files' symbol tables, in input order.
llvm::DenseMap<Symbol *, size_t> getFirstReferenceOrder(COFFLinkerContext &ctx);
llvm::DenseMap<ImportFile *, size_t> getImportLoadOrder(COFFLinkerContext &ctx);
size_t getFirstReference(const llvm::DenseMap<Symbol *, size_t> &refIndex,
                         ImportFile *file);
// Order imports as link.exe does: in the order their import library members
// were loaded (which follows link.exe's library search).
template <typename T, typename GetFile>
void sortImportsByFirstReference(COFFLinkerContext &ctx, std::vector<T> &v,
                                 GetFile getFile) {
  llvm::DenseMap<ImportFile *, size_t> loadIndex = getImportLoadOrder(ctx);
  llvm::stable_sort(v, [&](T a, T b) {
    return loadIndex.lookup(getFile(a)) < loadIndex.lookup(getFile(b));
  });
}

class IdataContents {
public:
  void add(DefinedImportData *sym) { imports.push_back(sym); }
  bool empty() { return imports.empty(); }

  void create(COFFLinkerContext &ctx);

private:
  void createLikeLinkExe(COFFLinkerContext &ctx);

public:
  std::vector<DefinedImportData *> imports;
  // Imports that were pulled in but aren't used. link.exe still counts them
  // when it orders the import tables.
  std::vector<DefinedImportData *> deadImports;
  std::vector<Chunk *> dirs;
  std::vector<Chunk *> lookups;
  std::vector<Chunk *> addresses;
  std::vector<Chunk *> hints;
  std::vector<Chunk *> dllNames;
  std::vector<Chunk *> auxIat;
  std::vector<Chunk *> auxIatCopy;
};

// Windows-specific.
// DelayLoadContents creates all chunks for the delay-load DLL import table.
class DelayLoadContents {
public:
  DelayLoadContents(COFFLinkerContext &ctx) : ctx(ctx) {}
  void add(DefinedImportData *sym) { imports.push_back(sym); }
  bool empty() { return imports.empty(); }
  void create();
  std::vector<Chunk *> getChunks();
  std::vector<Chunk *> getDataChunks();
  ArrayRef<Chunk *> getModuleHandles() { return moduleHandles; }
  ArrayRef<Chunk *> getAddresses() { return addresses; }
  ArrayRef<Chunk *> getCodeChunks() { return thunks; }
  ArrayRef<Chunk *> getCodePData() { return pdata; }
  ArrayRef<Chunk *> getCodeUnwindInfo() { return unwindinfo; }
  ArrayRef<Chunk *> getAuxIat() { return auxIat; }
  ArrayRef<Chunk *> getAuxIatCopy() { return auxIatCopy; }

  uint64_t getDirRVA() { return dirs[0]->getRVA(); }
  uint64_t getDirSize();

private:
  Chunk *newThunkChunk(DefinedImportData *s, Chunk *tailMerge);
  Chunk *newTailMergeChunk(SymbolTable &symtab, Chunk *dir);
  Chunk *newTailMergePDataChunk(SymbolTable &symtab, Chunk *tm);

  std::vector<DefinedImportData *> imports;
  // Imports that were pulled in but aren't used. link.exe still counts them
  // when it orders the import tables.
  std::vector<DefinedImportData *> deadImports;
  std::vector<Chunk *> dirs;
  std::vector<Chunk *> moduleHandles;
  std::vector<Chunk *> addresses;
  std::vector<Chunk *> names;
  std::vector<Chunk *> hintNames;
  std::vector<Chunk *> boundTabs;
  std::vector<Chunk *> thunks;
  std::vector<Chunk *> pdata;
  std::vector<Chunk *> unwindinfo;
  std::vector<Chunk *> dllNames;
  std::vector<Chunk *> auxIat;
  std::vector<Chunk *> auxIatCopy;

  COFFLinkerContext &ctx;
};

// Create all chunks for the DLL export table.
void createEdataChunks(SymbolTable &symtab, std::vector<Chunk *> &chunks);

} // namespace lld::coff

#endif
