//===- Target.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Target.h"
#include "COFFLinkerContext.h"
#include "Chunks.h"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "Writer.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"
#include <iterator>
#include <optional>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::COFF;

namespace lld::coff {

void writePPC32(uint8_t *off, uint32_t v, bool isLE) {
  if (isLE)
    write32le(off, v);
  else
    write32be(off, v);
}

namespace {

static void add16(uint8_t *p, int16_t v) { write16le(p, read16le(p) + v); }
static void add32(uint8_t *p, int32_t v) { write32le(p, read32le(p) + v); }
static void add64(uint8_t *p, int64_t v) { write64le(p, read64le(p) + v); }
static void or16(uint8_t *p, uint16_t v) { write16le(p, read16le(p) | v); }
static void or32(uint8_t *p, uint32_t v) { write32le(p, read32le(p) | v); }

// Verify that given sections are appropriate targets for SECREL
// relocations. This check is relaxed because unfortunately debug
// sections have section-relative relocations against absolute symbols.
static bool checkSecRel(const SectionChunk *sec, OutputSection *os) {
  if (os)
    return true;
  if (sec->isCodeView())
    return false;
  error("SECREL relocation cannot be applied to absolute symbols");
  return false;
}

static void applySecRel(const SectionChunk *sec, uint8_t *off,
                        OutputSection *os, uint64_t s) {
  if (!checkSecRel(sec, os))
    return;
  uint64_t secRel = s - os->getRVA();
  if (secRel > UINT32_MAX) {
    error("overflow in SECREL relocation in section: " + sec->getSectionName());
    return;
  }
  add32(off, secRel);
}

static void applySecIdx(uint8_t *off, OutputSection *os,
                        unsigned numOutputSections) {
  // numOutputSections is the largest valid section index. Make sure that
  // it fits in 16 bits.
  assert(numOutputSections <= 0xffff && "size of outputSections is too big");

  // Absolute symbol doesn't have section index, but section index relocation
  // against absolute symbol should be resolved to one plus the last output
  // section index. This is required for compatibility with MSVC.
  if (os)
    add16(off, os->sectionIndex);
  else
    add16(off, numOutputSections + 1);
}

static void applyAlphaBranch(uint8_t *off, uint64_t s, uint64_t p) {
  uint32_t inst = read32le(off);
  int64_t addend = SignExtend64<21>(inst & 0x1fffff) * 4;
  if ((int64_t(s) + addend) & 3) {
    error("Alpha branch relocation target is not four-byte aligned");
    return;
  }
  int64_t disp = (int64_t(s) + addend - int64_t(p) - 4) >> 2;
  if (!isInt<21>(disp)) {
    error("overflow in Alpha branch relocation");
    return;
  }
  write32le(off, (inst & 0xffe00000) | (uint32_t(disp) & 0x1fffff));
}

static uint16_t getAlphaRefQ3(uint64_t v) {
  return ((v + 0x80008000ULL) >> 32) & 0xffff;
}

static uint16_t getAlphaRefQ2(uint64_t v) {
  return ((v + 0x8000) >> 16) & 0xffff;
}

static uint16_t getAlphaRefQ1(uint64_t v) { return v & 0xffff; }

static void applyRelAlpha(COFFLinkerContext &ctx, const TargetInfo &target,
                          const SectionChunk &sec, uint8_t *off,
                          const coff_relocation &rel, OutputSection *os,
                          uint64_t s, uint64_t p, uint64_t imageBase) {
  switch (rel.Type) {
  case IMAGE_REL_ALPHA_ABSOLUTE:
    break;
  case IMAGE_REL_ALPHA_REFLONG:
    add32(off, s + imageBase);
    break;
  case IMAGE_REL_ALPHA_REFQUAD:
    add64(off, s + imageBase);
    break;
  case IMAGE_REL_ALPHA_GPREL32: {
    if (!ctx.alphaGlobalPointerRVA || s < ctx.alphaSmallDataStartRVA ||
        s >= ctx.alphaSmallDataEndRVA) {
      error("Alpha GP-relative relocation target is not in .sdata");
      break;
    }
    add32(off, s - *ctx.alphaGlobalPointerRVA);
    break;
  }
  case IMAGE_REL_ALPHA_LITERAL: {
    if (!ctx.alphaGlobalPointerRVA || s < ctx.alphaSmallDataStartRVA ||
        s >= ctx.alphaSmallDataEndRVA) {
      error("Alpha literal relocation target is not in .sdata");
      break;
    }
    int64_t value = SignExtend64<16>(read16le(off)) + int64_t(s) -
                    int64_t(*ctx.alphaGlobalPointerRVA);
    if (!isInt<16>(value)) {
      error("Alpha literal relocation out of range");
      break;
    }
    write16le(off, value);
    break;
  }
  case IMAGE_REL_ALPHA_LITUSE:
    break;
  case IMAGE_REL_ALPHA_BRADDR:
    applyAlphaBranch(off, s, p);
    break;
  case IMAGE_REL_ALPHA_REFHI: {
    uint64_t v = s + imageBase + (uint64_t(read16le(off)) << 16) +
                 target.getPairAddend(sec, rel);
    write16le(off, ((v + 0x8000) >> 16) & 0xffff);
    break;
  }
  case IMAGE_REL_ALPHA_REFLO:
    write16le(off, (s + imageBase + read16le(off)) & 0xffff);
    break;
  case IMAGE_REL_ALPHA_PAIR:
    break;
  case IMAGE_REL_ALPHA_REFQ3: {
    uint64_t v = s + imageBase + (uint64_t(read16le(off)) << 32) +
                 target.getPairAddend(sec, rel);
    write16le(off, getAlphaRefQ3(v));
    break;
  }
  case IMAGE_REL_ALPHA_REFQ2: {
    uint64_t v = s + imageBase + (uint64_t(read16le(off)) << 16) +
                 target.getPairAddend(sec, rel);
    write16le(off, getAlphaRefQ2(v));
    break;
  }
  case IMAGE_REL_ALPHA_REFQ1:
    write16le(off, getAlphaRefQ1(s + imageBase + read16le(off)));
    break;
  case IMAGE_REL_ALPHA_REFLONGNB:
    add32(off, s);
    break;
  case IMAGE_REL_ALPHA_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_ALPHA_SECREL:
    applySecRel(&sec, off, os, s);
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(rel.Type) +
          " in " + toString(sec.file));
  }
}

static void applyRelX64(COFFLinkerContext &ctx, const SectionChunk &sec,
                        uint8_t *off, uint16_t type, OutputSection *os,
                        uint64_t s, uint64_t p, uint64_t imageBase) {
  switch (type) {
  case IMAGE_REL_AMD64_ADDR32:
    add32(off, s + imageBase);
    break;
  case IMAGE_REL_AMD64_ADDR64:
    add64(off, s + imageBase);
    break;
  case IMAGE_REL_AMD64_ADDR32NB:
    add32(off, s);
    break;
  case IMAGE_REL_AMD64_REL32:
    add32(off, s - p - 4);
    break;
  case IMAGE_REL_AMD64_REL32_1:
    add32(off, s - p - 5);
    break;
  case IMAGE_REL_AMD64_REL32_2:
    add32(off, s - p - 6);
    break;
  case IMAGE_REL_AMD64_REL32_3:
    add32(off, s - p - 7);
    break;
  case IMAGE_REL_AMD64_REL32_4:
    add32(off, s - p - 8);
    break;
  case IMAGE_REL_AMD64_REL32_5:
    add32(off, s - p - 9);
    break;
  case IMAGE_REL_AMD64_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_AMD64_SECREL:
    applySecRel(&sec, off, os, s);
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(sec.file));
  }
}

static void applyRelX86(COFFLinkerContext &ctx, const SectionChunk &sec,
                        uint8_t *off, uint16_t type, OutputSection *os,
                        uint64_t s, uint64_t p, uint64_t imageBase) {
  switch (type) {
  case IMAGE_REL_I386_ABSOLUTE:
    break;
  case IMAGE_REL_I386_DIR32:
    add32(off, s + imageBase);
    break;
  case IMAGE_REL_I386_DIR32NB:
    add32(off, s);
    break;
  case IMAGE_REL_I386_REL32:
    add32(off, s - p - 4);
    break;
  case IMAGE_REL_I386_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_I386_SECREL:
    applySecRel(&sec, off, os, s);
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(sec.file));
  }
}

static uint16_t readPPC16(uint8_t *off, bool isLE) {
  return isLE ? read16le(off) : read16be(off);
}

static uint32_t readPPC32(const uint8_t *off, bool isLE) {
  return isLE ? read32le(off) : read32be(off);
}

static void writePPC16(uint8_t *off, uint16_t v, bool isLE) {
  if (isLE)
    write16le(off, v);
  else
    write16be(off, v);
}

static void applyPPCImm16(uint8_t *off, uint16_t v, bool isLE) {
  writePPC16(off, v, isLE);
}

static void applyPPCAddr24(uint8_t *off, uint64_t v, bool isLE) {
  uint32_t inst = readPPC32(off, isLE);
  writePPC32(off, (inst & 0xfc000003) | (v & 0x03fffffc), isLE);
}

static void applyPPCAddr14(uint8_t *off, uint64_t v, bool isLE) {
  uint32_t inst = readPPC32(off, isLE);
  writePPC32(off, (inst & 0xffff0003) | (v & 0x0000fffc), isLE);
}

static std::optional<uint64_t> getPPCTocRVA(const SectionChunk *sec) {
  Symbol *toc = sec->file->symtab.ctx.symtab.find(".toc");
  if (auto *d = dyn_cast_or_null<Defined>(toc))
    return d->getRVA();

  error("PowerPC TOC-relative relocation without .toc definition in " +
        toString(sec->file));
  return std::nullopt;
}

static std::optional<uint32_t> getPPCImportGlueInst(Symbol *sym) {
  auto *d = dyn_cast_or_null<DefinedRegular>(sym);
  if (!d)
    return std::nullopt;

  auto *obj = dyn_cast<ObjFile>(d->getFile());
  if (!obj)
    return std::nullopt;

  for (Chunk *chunk : obj->getChunks()) {
    auto *sec = dyn_cast<SectionChunk>(chunk);
    if (!sec)
      continue;
    for (const coff_relocation &rel : sec->getRelocs()) {
      if ((rel.Type & IMAGE_REL_PPC_TYPEMASK) != IMAGE_REL_PPC_IMGLUE)
        continue;
      if (obj->getSymbol(rel.SymbolTableIndex) == sym)
        return rel.VirtualAddress;
    }
  }

  return std::nullopt;
}

static void applyRelPPC(COFFLinkerContext &ctx, const TargetInfo &target,
                        const SectionChunk &sec, uint8_t *off,
                        const coff_relocation &rel, OutputSection *os,
                        uint64_t s, uint64_t p, uint64_t imageBase) {
  bool isLE = sec.getArch() == Triple::ppcle;
  uint16_t type = rel.Type & IMAGE_REL_PPC_TYPEMASK;
  uint64_t va = s + imageBase;
  switch (type) {
  case IMAGE_REL_PPC_ABSOLUTE:
  case IMAGE_REL_PPC_PAIR:
  case IMAGE_REL_PPC_IMGLUE:
    break;
  case IMAGE_REL_PPC_IFGLUE:
    if (std::optional<uint32_t> inst =
            getPPCImportGlueInst(sec.file->getSymbol(rel.SymbolTableIndex)))
      writePPC32(off, *inst, isLE);
    break;
  case IMAGE_REL_PPC_ADDR64:
    add64(off, va);
    break;
  case IMAGE_REL_PPC_ADDR32:
    writePPC32(off, readPPC32(off, isLE) + va, isLE);
    break;
  case IMAGE_REL_PPC_ADDR32NB:
    writePPC32(off, readPPC32(off, isLE) + s, isLE);
    break;
  case IMAGE_REL_PPC_ADDR24:
    applyPPCAddr24(off, va + (readPPC32(off, isLE) & 0x03fffffc), isLE);
    break;
  case IMAGE_REL_PPC_ADDR14:
    applyPPCAddr14(off, va + (readPPC32(off, isLE) & 0x0000fffc), isLE);
    break;
  case IMAGE_REL_PPC_REL24: {
    // Microsoft PowerPC branch relocations are relative to the beginning of
    // the input section contribution. The assembler biases the signed addend
    // by the relocation's offset within that contribution.
    int64_t sectionStart =
        static_cast<int64_t>(p) - static_cast<int64_t>(rel.VirtualAddress);
    int64_t addend = SignExtend64<26>(readPPC32(off, isLE) & 0x03fffffc);
    int64_t value = static_cast<int64_t>(s) - sectionStart + addend;
    if (!isInt<26>(value))
      error("PowerPC REL24 relocation out of range");
    if (value & 3)
      error("PowerPC REL24 relocation is not four-byte aligned");
    applyPPCAddr24(off, value, isLE);
    break;
  }
  case IMAGE_REL_PPC_REL14: {
    int64_t sectionStart =
        static_cast<int64_t>(p) - static_cast<int64_t>(rel.VirtualAddress);
    int64_t addend = SignExtend64<16>(readPPC32(off, isLE) & 0x0000fffc);
    applyPPCAddr14(off, static_cast<int64_t>(s) - sectionStart + addend, isLE);
    break;
  }
  case IMAGE_REL_PPC_TOCREL16:
    if (std::optional<uint64_t> tocRVA = getPPCTocRVA(&sec)) {
      int64_t v = static_cast<int64_t>(s) - static_cast<int64_t>(*tocRVA) +
                  readPPC16(off, isLE);
      applyPPCImm16(off, v, isLE);
    }
    break;
  case IMAGE_REL_PPC_TOCREL14:
    if (std::optional<uint64_t> tocRVA = getPPCTocRVA(&sec)) {
      int64_t v = static_cast<int64_t>(s) - static_cast<int64_t>(*tocRVA) +
                  (readPPC32(off, isLE) & 0x0000fffc);
      applyPPCAddr14(off, v, isLE);
    }
    break;
  case IMAGE_REL_PPC_REFHI: {
    uint64_t v = va + (uint64_t(readPPC16(off, isLE)) << 16) +
                 target.getPairAddend(sec, rel);
    applyPPCImm16(off, ((v + 0x8000) >> 16) & 0xffff, isLE);
    break;
  }
  case IMAGE_REL_PPC_REFLO:
    applyPPCImm16(off, (va + readPPC16(off, isLE)) & 0xffff, isLE);
    break;
  case IMAGE_REL_PPC_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_PPC_SECREL:
    applySecRel(&sec, off, os, s);
    break;
  case IMAGE_REL_PPC_SECRELLO:
    if (checkSecRel(&sec, os))
      applyPPCImm16(off, (s - os->getRVA() + readPPC16(off, isLE)) & 0xffff,
                    isLE);
    break;
  case IMAGE_REL_PPC_SECRELHI:
    if (checkSecRel(&sec, os)) {
      uint64_t v = s - os->getRVA() + (uint64_t(readPPC16(off, isLE)) << 16) +
                   target.getPairAddend(sec, rel);
      applyPPCImm16(off, ((v + 0x8000) >> 16) & 0xffff, isLE);
    }
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(rel.Type) +
          " in " + toString(sec.file));
  }
}

static void applyMOV(uint8_t *off, uint16_t v) {
  write16le(off,
            (read16le(off) & 0xfbf0) | ((v & 0x800) >> 1) | ((v >> 12) & 0xf));
  write16le(off + 2,
            (read16le(off + 2) & 0x8f00) | ((v & 0x700) << 4) | (v & 0xff));
}

static uint16_t readMOV(uint8_t *off, bool movt) {
  uint16_t op1 = read16le(off);
  if ((op1 & 0xfbf0) != (movt ? 0xf2c0 : 0xf240))
    error("unexpected instruction in " + Twine(movt ? "MOVT" : "MOVW") +
          " instruction in MOV32T relocation");
  uint16_t op2 = read16le(off + 2);
  if ((op2 & 0x8000) != 0)
    error("unexpected instruction in " + Twine(movt ? "MOVT" : "MOVW") +
          " instruction in MOV32T relocation");
  return (op2 & 0x00ff) | ((op2 >> 4) & 0x0700) | ((op1 << 1) & 0x0800) |
         ((op1 & 0x000f) << 12);
}

static void applyBranch20T(uint8_t *off, int32_t v) {
  if (!isInt<21>(v))
    error("relocation out of range");
  uint32_t s = v < 0 ? 1 : 0;
  uint32_t j1 = (v >> 19) & 1;
  uint32_t j2 = (v >> 18) & 1;
  or16(off, (s << 10) | ((v >> 12) & 0x3f));
  or16(off + 2, (j1 << 13) | (j2 << 11) | ((v >> 1) & 0x7ff));
}

static void applyRelARM(COFFLinkerContext &ctx, const SectionChunk &sec,
                        uint8_t *off, uint16_t type, OutputSection *os,
                        uint64_t s, uint64_t p, uint64_t imageBase) {
  // Pointer to thumb code must have the LSB set.
  uint64_t sx = s;
  if (os && (os->header.Characteristics & IMAGE_SCN_MEM_EXECUTE))
    sx |= 1;
  switch (type) {
  case IMAGE_REL_ARM_ADDR32:
    add32(off, sx + imageBase);
    break;
  case IMAGE_REL_ARM_ADDR32NB:
    add32(off, sx);
    break;
  case IMAGE_REL_ARM_MOV32T:
    applyMOV32T(off, sx + imageBase);
    break;
  case IMAGE_REL_ARM_BRANCH20T:
    applyBranch20T(off, sx - p - 4);
    break;
  case IMAGE_REL_ARM_BRANCH24T:
    applyBranch24T(off, sx - p - 4);
    break;
  case IMAGE_REL_ARM_BLX23T:
    applyBranch24T(off, sx - p - 4);
    break;
  case IMAGE_REL_ARM_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_ARM_SECREL:
    applySecRel(&sec, off, os, s);
    break;
  case IMAGE_REL_ARM_REL32:
    add32(off, sx - p - 4);
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(sec.file));
  }
}

static void applySecRelLow12A(const SectionChunk *sec, uint8_t *off,
                              OutputSection *os, uint64_t s) {
  if (checkSecRel(sec, os))
    applyArm64Imm(off, (s - os->getRVA()) & 0xfff, 0);
}

static void applySecRelHigh12A(const SectionChunk *sec, uint8_t *off,
                               OutputSection *os, uint64_t s) {
  if (!checkSecRel(sec, os))
    return;
  uint32_t orig = read32le(off);
  uint64_t imm = (orig >> 10) & 0xFFF;
  orig &= ~(0xFFF << 10);
  imm = (s + imm - os->getRVA()) >> 12;
  if (0xfff < imm) {
    error("overflow in SECREL_HIGH12A relocation in section: " +
          sec->getSectionName());
    return;
  }
  write32le(off, orig | (imm << 10));
}

static void applySecRelLdr(const SectionChunk *sec, uint8_t *off,
                           OutputSection *os, uint64_t s) {
  if (checkSecRel(sec, os))
    applyArm64Ldr(off, (s - os->getRVA()) & 0xfff);
}

static void applyArm64Branch19(uint8_t *off, int64_t v) {
  if (!isInt<21>(v))
    error("relocation out of range");
  or32(off, (v & 0x001FFFFC) << 3);
}

static void applyArm64Branch14(uint8_t *off, int64_t v) {
  if (!isInt<16>(v))
    error("relocation out of range");
  or32(off, (v & 0x0000FFFC) << 3);
}

static void applyRelARM64(COFFLinkerContext &ctx, const SectionChunk &sec,
                          uint8_t *off, uint16_t type, OutputSection *os,
                          uint64_t s, uint64_t p, uint64_t imageBase) {
  switch (type) {
  case IMAGE_REL_ARM64_PAGEBASE_REL21:
    applyArm64Addr(off, s, p, 12);
    break;
  case IMAGE_REL_ARM64_REL21:
    applyArm64Addr(off, s, p, 0);
    break;
  case IMAGE_REL_ARM64_PAGEOFFSET_12A:
    applyArm64Imm(off, s & 0xfff, 0);
    break;
  case IMAGE_REL_ARM64_PAGEOFFSET_12L:
    applyArm64Ldr(off, s & 0xfff);
    break;
  case IMAGE_REL_ARM64_BRANCH26:
    applyArm64Branch26(off, s - p);
    break;
  case IMAGE_REL_ARM64_BRANCH19:
    applyArm64Branch19(off, s - p);
    break;
  case IMAGE_REL_ARM64_BRANCH14:
    applyArm64Branch14(off, s - p);
    break;
  case IMAGE_REL_ARM64_ADDR32:
    add32(off, s + imageBase);
    break;
  case IMAGE_REL_ARM64_ADDR32NB:
    add32(off, s);
    break;
  case IMAGE_REL_ARM64_ADDR64:
    add64(off, s + imageBase);
    break;
  case IMAGE_REL_ARM64_SECREL:
    applySecRel(&sec, off, os, s);
    break;
  case IMAGE_REL_ARM64_SECREL_LOW12A:
    applySecRelLow12A(&sec, off, os, s);
    break;
  case IMAGE_REL_ARM64_SECREL_HIGH12A:
    applySecRelHigh12A(&sec, off, os, s);
    break;
  case IMAGE_REL_ARM64_SECREL_LOW12L:
    applySecRelLdr(&sec, off, os, s);
    break;
  case IMAGE_REL_ARM64_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_ARM64_REL32:
    add32(off, s - p - 4);
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(type) + " in " +
          toString(sec.file));
  }
}

} // namespace

static int64_t readRISCVUType(const uint8_t *off) {
  return int32_t(read32le(off) & 0xfffff000);
}

static int64_t readRISCVIType(const uint8_t *off) {
  return SignExtend64<12>(read32le(off) >> 20);
}

static int64_t readRISCVSType(const uint8_t *off) {
  uint32_t inst = read32le(off);
  uint32_t imm = ((inst >> 25) << 5) | ((inst >> 7) & 0x1f);
  return SignExtend64<12>(imm);
}

static int64_t readRISCVBranch(const uint8_t *off) {
  uint32_t inst = read32le(off);
  uint32_t imm = ((inst >> 31) << 12) | (((inst >> 7) & 1) << 11) |
                 (((inst >> 25) & 0x3f) << 5) |
                 (((inst >> 8) & 0xf) << 1);
  return SignExtend64<13>(imm);
}

static int64_t readRISCVJAL(const uint8_t *off) {
  uint32_t inst = read32le(off);
  uint32_t imm = ((inst >> 31) << 20) | (((inst >> 12) & 0xff) << 12) |
                 (((inst >> 20) & 1) << 11) |
                 (((inst >> 21) & 0x3ff) << 1);
  return SignExtend64<21>(imm);
}

static int64_t readRISCVRVCJump(const uint8_t *off) {
  uint16_t inst = read16le(off);
  uint16_t imm = ((inst >> 12) & 1) << 11 | ((inst >> 11) & 1) << 4 |
                 ((inst >> 9) & 3) << 8 | ((inst >> 8) & 1) << 10 |
                 ((inst >> 7) & 1) << 6 | ((inst >> 6) & 1) << 7 |
                 ((inst >> 3) & 7) << 1 | ((inst >> 2) & 1) << 5;
  return SignExtend64<12>(imm);
}

static int64_t readRISCVRVCBranch(const uint8_t *off) {
  uint16_t inst = read16le(off);
  uint16_t imm = ((inst >> 12) & 1) << 8 | ((inst >> 10) & 3) << 3 |
                 ((inst >> 5) & 3) << 6 | ((inst >> 3) & 3) << 1 |
                 ((inst >> 2) & 1) << 5;
  return SignExtend64<9>(imm);
}

bool applyRISCVUType(uint8_t *off, int64_t value,
                            const Twine &context) {
  int64_t high = (value + 0x800) >> 12;
  if (!isInt<20>(high)) {
    error("RISC-V HI20 relocation out of range in " + context + ": " + Twine(value));
    return false;
  }
  uint32_t inst = read32le(off);
  write32le(off, (inst & 0xfff) | (uint32_t(value + 0x800) & 0xfffff000));
  return true;
}

void applyRISCVIType(uint8_t *off, int64_t value) {
  uint32_t inst = read32le(off);
  write32le(off, (inst & 0x000fffff) | ((uint32_t(value) & 0xfff) << 20));
}

static void applyRISCVSType(uint8_t *off, int64_t value) {
  uint32_t inst = read32le(off);
  uint32_t imm = uint32_t(value) & 0xfff;
  write32le(off, (inst & 0x01fff07f) | ((imm & 0xfe0) << 20) | ((imm & 0x1f) << 7));
}

static void applyRISCVBranch(uint8_t *off, int64_t value) {
  if (!isInt<13>(value)) {
    error("RISC-V branch relocation out of range");
    return;
  }
  if (value & 1) {
    error("RISC-V branch relocation is not two-byte aligned");
    return;
  }
  uint32_t inst = read32le(off) & 0x01fff07f;
  uint32_t imm = uint32_t(value);
  inst |= ((imm >> 12) & 1) << 31;
  inst |= ((imm >> 5) & 0x3f) << 25;
  inst |= ((imm >> 1) & 0xf) << 8;
  inst |= ((imm >> 11) & 1) << 7;
  write32le(off, inst);
}

static void applyRISCVJAL(uint8_t *off, int64_t value) {
  if (!isInt<21>(value)) {
    error("RISC-V JAL relocation out of range");
    return;
  }
  if (value & 1) {
    error("RISC-V JAL relocation is not two-byte aligned");
    return;
  }
  uint32_t inst = read32le(off) & 0xfff;
  uint32_t imm = uint32_t(value);
  inst |= ((imm >> 20) & 1) << 31;
  inst |= ((imm >> 1) & 0x3ff) << 21;
  inst |= ((imm >> 11) & 1) << 20;
  inst |= ((imm >> 12) & 0xff) << 12;
  write32le(off, inst);
}

static void applyRISCVRVCJump(uint8_t *off, int64_t value) {
  if (!isInt<12>(value)) {
    error("RISC-V compressed jump relocation out of range");
    return;
  }
  if (value & 1) {
    error("RISC-V compressed jump relocation is not two-byte aligned");
    return;
  }
  uint16_t inst = read16le(off) & 0xe003;
  uint16_t imm = uint16_t(value);
  inst |= ((imm >> 11) & 1) << 12;
  inst |= ((imm >> 4) & 1) << 11;
  inst |= ((imm >> 8) & 3) << 9;
  inst |= ((imm >> 10) & 1) << 8;
  inst |= ((imm >> 6) & 1) << 7;
  inst |= ((imm >> 7) & 1) << 6;
  inst |= ((imm >> 1) & 7) << 3;
  inst |= ((imm >> 5) & 1) << 2;
  write16le(off, inst);
}

static void applyRISCVRVCBranch(uint8_t *off, int64_t value) {
  if (!isInt<9>(value)) {
    error("RISC-V compressed branch relocation out of range");
    return;
  }
  if (value & 1) {
    error("RISC-V compressed branch relocation is not two-byte aligned");
    return;
  }
  uint16_t inst = read16le(off) & 0xe383;
  uint16_t imm = uint16_t(value);
  inst |= ((imm >> 8) & 1) << 12;
  inst |= ((imm >> 3) & 3) << 10;
  inst |= ((imm >> 6) & 3) << 5;
  inst |= ((imm >> 1) & 3) << 3;
  inst |= ((imm >> 5) & 1) << 2;
  write16le(off, inst);
}

static bool isRISCVLowType(uint16_t type, uint16_t lowIType,
                           uint16_t lowSType) {
  return type == lowIType || type == lowSType;
}

static const coff_relocation *
findRISCVHighForLow(const SectionChunk *sec, const coff_relocation &low,
                    uint16_t highType) {
  ArrayRef<coff_relocation> relocs = sec->getRelocs();
  auto current = llvm::find_if(relocs, [&](const coff_relocation &rel) { return &rel == &low; });
  while (current != relocs.begin()) {
    --current;
    if (current->Type == highType && current->SymbolTableIndex == low.SymbolTableIndex)
      return &*current;
  }
  return nullptr;
}

static const coff_relocation *
findRISCVLowForHigh(const SectionChunk *sec, const coff_relocation &high,
                    uint16_t highType, uint16_t lowIType,
                    uint16_t lowSType) {
  ArrayRef<coff_relocation> relocs = sec->getRelocs();
  auto current = llvm::find_if(relocs, [&](const coff_relocation &rel) { return &rel == &high; });
  if (current == relocs.end())
    return nullptr;
  for (++current; current != relocs.end(); ++current) {
    if (!isRISCVLowType(current->Type, lowIType, lowSType) || current->SymbolTableIndex != high.SymbolTableIndex)
      continue;
    if (findRISCVHighForLow(sec, *current, highType) == &high)
      return &*current;
  }
  return nullptr;
}

static std::optional<int64_t>
getRISCVSplitAddend(const SectionChunk *sec, const coff_relocation &rel,
                    uint16_t highType, uint16_t lowIType, uint16_t lowSType,
                    const coff_relocation *&high) {
  const coff_relocation *low;
  if (rel.Type == highType) {
    high = &rel;
    low = findRISCVLowForHigh(sec, rel, highType, lowIType, lowSType);
  } else {
    low = &rel;
    high = findRISCVHighForLow(sec, rel, highType);
  }
  if (!high || !low) {
    error("RISC-V split relocation has no compatible high/low pair in " + toString(sec->file));
    return std::nullopt;
  }

  ArrayRef<uint8_t> data = sec->getContents();
  if (high->VirtualAddress > data.size() ||
      data.size() - high->VirtualAddress < 4 ||
      low->VirtualAddress > data.size() || data.size() - low->VirtualAddress < 4) {
    error("RISC-V split relocation points beyond its input section in " + toString(sec->file));
    return std::nullopt;
  }
  uint32_t highInst = read32le(data.data() + high->VirtualAddress);
  uint32_t expectedOpcode = highType == IMAGE_REL_RISCV_PCREL_HI20 ? 0x17 : 0x37;
  if ((highInst & 0x7f) != expectedOpcode) {
    error("RISC-V split relocation does not reference the expected high instruction in " + toString(sec->file));
    return std::nullopt;
  }

  int64_t lowAddend = low->Type == lowIType
                           ? readRISCVIType(data.data() + low->VirtualAddress)
                           : readRISCVSType(data.data() + low->VirtualAddress);
  return readRISCVUType(data.data() + high->VirtualAddress) + lowAddend;
}

static void applyRelRISCV(COFFLinkerContext &ctx, const SectionChunk *sec,
                          uint8_t *off, const coff_relocation &rel,
                          OutputSection *os, uint64_t s, uint64_t p,
                          uint64_t imageBase) {
  auto requireBytes = [&](size_t count) {
    if (rel.VirtualAddress <= sec->getSize() && count <= sec->getSize() - rel.VirtualAddress)
      return true;
    error("RISC-V relocation points beyond its input section in " + toString(sec->file));
    return false;
  };

  switch (rel.Type) {
  case IMAGE_REL_RISCV_ABSOLUTE:
    break;
  case IMAGE_REL_RISCV_ADDR32:
    if (requireBytes(4))
      add32(off, s + imageBase);
    break;
  case IMAGE_REL_RISCV_ADDR32NB:
    if (requireBytes(4))
      add32(off, s);
    break;
  case IMAGE_REL_RISCV_ADDR64:
    if (requireBytes(8))
      add64(off, s + imageBase);
    break;
  case IMAGE_REL_RISCV_REL32:
    if (requireBytes(4))
      add32(off, s - p - 4);
    break;
  case IMAGE_REL_RISCV_BRANCH:
    if (requireBytes(4)) {
      if ((read32le(off) & 0x7f) != 0x63) {
        error("RISC-V branch relocation does not reference a branch instruction");
        break;
      }
      applyRISCVBranch(off, int64_t(s) + readRISCVBranch(off) - int64_t(p));
    }
    break;
  case IMAGE_REL_RISCV_JAL:
    if (requireBytes(4)) {
      if ((read32le(off) & 0x7f) != 0x6f) {
        error("RISC-V JAL relocation does not reference a JAL instruction");
        break;
      }
      applyRISCVJAL(off, int64_t(s) + readRISCVJAL(off) - int64_t(p));
    }
    break;
  case IMAGE_REL_RISCV_CALL:
    if (requireBytes(8)) {
      if ((read32le(off) & 0x7f) != 0x17 || (read32le(off + 4) & 0x707f) != 0x67) {
        error("RISC-V CALL relocation does not reference an AUIPC/JALR pair");
        break;
      }
      int64_t value = int64_t(s) + readRISCVUType(off) + readRISCVIType(off + 4) - int64_t(p);
      if (applyRISCVUType(off, value, toString(sec->file)))
        applyRISCVIType(off + 4, value);
    }
    break;
  case IMAGE_REL_RISCV_PCREL_HI20:
  case IMAGE_REL_RISCV_PCREL_LO12_I:
  case IMAGE_REL_RISCV_PCREL_LO12_S: {
    if (!requireBytes(4))
      break;
    const coff_relocation *high = nullptr;
    std::optional<int64_t> addend = getRISCVSplitAddend(sec, rel, IMAGE_REL_RISCV_PCREL_HI20, IMAGE_REL_RISCV_PCREL_LO12_I, IMAGE_REL_RISCV_PCREL_LO12_S, high);
    if (!addend)
      break;
    int64_t value = int64_t(s) + *addend - int64_t(sec->getRVA() + high->VirtualAddress);
    if (rel.Type == IMAGE_REL_RISCV_PCREL_HI20)
      applyRISCVUType(off, value, toString(sec->file));
    else if (rel.Type == IMAGE_REL_RISCV_PCREL_LO12_I)
      applyRISCVIType(off, value);
    else
      applyRISCVSType(off, value);
    break;
  }
  case IMAGE_REL_RISCV_HI20:
  case IMAGE_REL_RISCV_LO12_I:
  case IMAGE_REL_RISCV_LO12_S: {
    if (!requireBytes(4))
      break;
    const coff_relocation *high = nullptr;
    std::optional<int64_t> addend = getRISCVSplitAddend(sec, rel, IMAGE_REL_RISCV_HI20, IMAGE_REL_RISCV_LO12_I, IMAGE_REL_RISCV_LO12_S, high);
    if (!addend)
      break;
    int64_t value = int64_t(imageBase) + int64_t(s) + *addend;
    if (rel.Type == IMAGE_REL_RISCV_HI20)
      applyRISCVUType(off, value, toString(sec->file));
    else if (rel.Type == IMAGE_REL_RISCV_LO12_I)
      applyRISCVIType(off, value);
    else
      applyRISCVSType(off, value);
    break;
  }
  case IMAGE_REL_RISCV_SECTION:
    if (requireBytes(2))
      applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_RISCV_SECREL:
    if (requireBytes(4))
      applySecRel(sec, off, os, s);
    break;
  case IMAGE_REL_RISCV_RVC_JUMP:
    if (requireBytes(2))
      applyRISCVRVCJump(off, int64_t(s) + readRISCVRVCJump(off) - int64_t(p));
    break;
  case IMAGE_REL_RISCV_RVC_BRANCH:
    if (requireBytes(2))
      applyRISCVRVCBranch(off, int64_t(s) + readRISCVRVCBranch(off) - int64_t(p));
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(rel.Type) + " in " + toString(sec->file));
  }
}

namespace {

static void applyMipsImm16(uint8_t *off, uint16_t v) { write16le(off, v); }

static void applyMipsJmpAddr(uint8_t *off, uint64_t v) {
  if (v & 3)
    error("misaligned jmp offset");
  uint32_t inst = read32le(off);
  write32le(off, (inst & 0xfc000000) | ((v >> 2) & 0x03ffffff));
}

static void applyRelMIPS(COFFLinkerContext &ctx, const TargetInfo &target,
                         const SectionChunk &sec, uint8_t *off,
                         const coff_relocation &rel, OutputSection *os,
                         uint64_t s, uint64_t, uint64_t imageBase) {
  uint64_t va = s + imageBase;
  switch (rel.Type) {
  case IMAGE_REL_MIPS_ABSOLUTE:
  case IMAGE_REL_MIPS_PAIR:
    break;
  case IMAGE_REL_MIPS_REFWORD:
    add32(off, va);
    break;
  case IMAGE_REL_MIPS_REFWORDNB:
    add32(off, s);
    break;
  case IMAGE_REL_MIPS_JMPADDR:
    applyMipsJmpAddr(off, va + (read32le(off) & 0x03ffffff) * 4);
    break;
  case IMAGE_REL_MIPS_REFHI: {
    uint64_t v =
        va + (uint64_t(read16le(off)) << 16) + target.getPairAddend(sec, rel);
    applyMipsImm16(off, ((v + 0x8000) >> 16) & 0xffff);
    break;
  }
  case IMAGE_REL_MIPS_REFLO:
    applyMipsImm16(off, (va + read16le(off)) & 0xffff);
    break;
  case IMAGE_REL_MIPS_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_MIPS_SECREL:
    applySecRel(&sec, off, os, s);
    break;
  case IMAGE_REL_MIPS_SECRELLO:
    if (checkSecRel(&sec, os))
      applyMipsImm16(off, (s - os->getRVA() + read16le(off)) & 0xffff);
    break;
  case IMAGE_REL_MIPS_SECRELHI:
    if (checkSecRel(&sec, os)) {
      uint64_t v = s - os->getRVA() + (uint64_t(read16le(off)) << 16) +
                   target.getPairAddend(sec, rel);
      applyMipsImm16(off, ((v + 0x8000) >> 16) & 0xffff);
    }
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(rel.Type) +
          " in " + toString(sec.file));
  }
}

static BaserelInfo makeBaserel(const coff_relocation &rel, uint8_t type,
                               uint16_t extra = 0) {
  return {rel.VirtualAddress, type, extra};
}

static std::optional<BaserelInfo> getBaserelX64(const coff_relocation &rel) {
  switch (rel.Type) {
  case IMAGE_REL_AMD64_ADDR64:
    return makeBaserel(rel, IMAGE_REL_BASED_DIR64);
  case IMAGE_REL_AMD64_ADDR32:
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
  default:
    return std::nullopt;
  }
}

static std::optional<BaserelInfo> getBaserelX86(const coff_relocation &rel) {
  if (rel.Type == IMAGE_REL_I386_DIR32)
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
  return std::nullopt;
}

static std::optional<BaserelInfo>
getBaserelAlpha(COFFLinkerContext &ctx, const TargetInfo &target,
                const SectionChunk &sec, const coff_relocation &rel,
                uint64_t targetRVA, std::optional<uint16_t> relocationValue) {
  switch (rel.Type) {
  case IMAGE_REL_ALPHA_REFLONG:
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
  case IMAGE_REL_ALPHA_REFQUAD:
    return makeBaserel(rel, IMAGE_REL_BASED_DIR64);
  case IMAGE_REL_ALPHA_REFHI: {
    if (!relocationValue)
      return std::nullopt;
    uint64_t v = targetRVA + ctx.config.imageBase +
                 (uint64_t(*relocationValue) << 16) +
                 target.getPairAddend(sec, rel);
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHADJ, v & 0xffff);
  }
  case IMAGE_REL_ALPHA_REFLO:
    return makeBaserel(rel, IMAGE_REL_BASED_LOW);
  default:
    return std::nullopt;
  }
}

static std::optional<BaserelInfo> getBaserelARM(const coff_relocation &rel) {
  switch (rel.Type) {
  case IMAGE_REL_ARM_ADDR32:
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
  case IMAGE_REL_ARM_MOV32T:
    return makeBaserel(rel, IMAGE_REL_BASED_ARM_MOV32T);
  default:
    return std::nullopt;
  }
}

static std::optional<BaserelInfo> getBaserelARM64(const coff_relocation &rel) {
  if (rel.Type == IMAGE_REL_ARM64_ADDR64)
    return makeBaserel(rel, IMAGE_REL_BASED_DIR64);
  return std::nullopt;
}

static std::optional<BaserelInfo> getBaserelRISCV(const coff_relocation &rel) {
  switch (rel.Type) {
  case IMAGE_REL_RISCV_ADDR32:
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
  case IMAGE_REL_RISCV_ADDR64:
    return makeBaserel(rel, IMAGE_REL_BASED_DIR64);
  case IMAGE_REL_RISCV_HI20:
    return makeBaserel(rel, IMAGE_REL_BASED_RISCV_HIGH20);
  case IMAGE_REL_RISCV_LO12_I:
    return makeBaserel(rel, IMAGE_REL_BASED_RISCV_LOW12I);
  case IMAGE_REL_RISCV_LO12_S:
    return makeBaserel(rel, IMAGE_REL_BASED_RISCV_LOW12S);
  default:
    return std::nullopt;
  }
}

static std::optional<BaserelInfo>
getBaserelMIPS(COFFLinkerContext &ctx, const TargetInfo &target,
               const SectionChunk &sec, const coff_relocation &rel,
               uint64_t targetRVA, std::optional<uint16_t> relocationValue) {
  switch (rel.Type) {
  case IMAGE_REL_MIPS_REFHALF:
    return makeBaserel(rel, IMAGE_REL_BASED_HIGH);
  case IMAGE_REL_MIPS_REFWORD:
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
  case IMAGE_REL_MIPS_REFHI: {
    if (!relocationValue)
      return std::nullopt;
    uint64_t v = targetRVA + ctx.config.imageBase +
                 (uint64_t(*relocationValue) << 16) +
                 target.getPairAddend(sec, rel);
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHADJ, v & 0xffff);
  }
  case IMAGE_REL_MIPS_REFLO:
    return makeBaserel(rel, IMAGE_REL_BASED_LOW);
  case IMAGE_REL_MIPS_JMPADDR:
    return makeBaserel(rel, IMAGE_REL_BASED_MIPS_JMPADDR);
  default:
    return std::nullopt;
  }
}

static std::optional<BaserelInfo>
getBaserelPPC(COFFLinkerContext &ctx, const TargetInfo &target,
              const SectionChunk &sec, const coff_relocation &rel,
              uint64_t targetRVA, std::optional<uint16_t> relocationValue) {
  switch (rel.Type & IMAGE_REL_PPC_TYPEMASK) {
  case IMAGE_REL_PPC_ADDR64:
    return makeBaserel(rel, IMAGE_REL_BASED_DIR64);
  case IMAGE_REL_PPC_ADDR32:
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
  case IMAGE_REL_PPC_REFHI: {
    if (!relocationValue)
      return std::nullopt;
    uint64_t v = targetRVA + ctx.config.imageBase +
                 (uint64_t(*relocationValue) << 16) +
                 target.getPairAddend(sec, rel);
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHADJ, v & 0xffff);
  }
  case IMAGE_REL_PPC_REFLO:
    return makeBaserel(rel, IMAGE_REL_BASED_LOW);
  default:
    return std::nullopt;
  }
}

class AMD64TargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    applyRelX64(ctx, sec, off, rel.Type, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &, const coff_relocation &rel, uint64_t,
             std::optional<uint16_t>) const override {
    return getBaserelX64(rel);
  }
};

class I386TargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    applyRelX86(ctx, sec, off, rel.Type, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &, const coff_relocation &rel, uint64_t,
             std::optional<uint16_t>) const override {
    return getBaserelX86(rel);
  }
};

class AlphaTargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  bool isMetadataRelocation(const coff_relocation &rel) const override {
    return rel.Type == IMAGE_REL_ALPHA_PAIR;
  }

  int64_t getPairAddend(const SectionChunk &sec,
                        const coff_relocation &rel) const override {
    bool isRefQ = rel.Type == IMAGE_REL_ALPHA_REFQ3;
    assert(rel.Type == IMAGE_REL_ALPHA_REFHI ||
           rel.Type == IMAGE_REL_ALPHA_REFQ2 || isRefQ);
    for (const coff_relocation &pair : sec.getRelocs())
      if (pair.VirtualAddress == rel.VirtualAddress &&
          pair.Type == IMAGE_REL_ALPHA_PAIR)
        return isRefQ ? SignExtend64<32>(pair.SymbolTableIndex)
                      : SignExtend64<16>(pair.SymbolTableIndex & 0xffff);

    error(Twine("Alpha ") + (isRefQ ? "REFQ" : "REFHI") +
          " relocation without matching PAIR relocation in " +
          toString(sec.file));
    return 0;
  }

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    applyRelAlpha(ctx, *this, sec, off, rel, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &sec, const coff_relocation &rel,
             uint64_t targetRVA,
             std::optional<uint16_t> relocationValue) const override {
    return getBaserelAlpha(ctx, *this, sec, rel, targetRVA, relocationValue);
  }
};

class ARMTargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    applyRelARM(ctx, sec, off, rel.Type, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &, const coff_relocation &rel, uint64_t,
             std::optional<uint16_t>) const override {
    return getBaserelARM(rel);
  }
};

class ARM64TargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    if (sec.getArch() == Triple::x86_64) {
      applyRelX64(ctx, sec, off, rel.Type, os, s, p, ctx.config.imageBase);
      return;
    }
    applyRelARM64(ctx, sec, off, rel.Type, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &sec, const coff_relocation &rel, uint64_t,
             std::optional<uint16_t>) const override {
    if (sec.getArch() == Triple::x86_64)
      return getBaserelX64(rel);
    return getBaserelARM64(rel);
  }
};

class RISCVTargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    applyRelRISCV(ctx, &sec, off, rel, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &, const coff_relocation &rel, uint64_t,
             std::optional<uint16_t>) const override {
    return getBaserelRISCV(rel);
  }
};

class MIPSTargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  bool isMetadataRelocation(const coff_relocation &rel) const override {
    return rel.Type == IMAGE_REL_MIPS_PAIR;
  }

  int64_t getPairAddend(const SectionChunk &sec,
                        const coff_relocation &rel) const override {
    assert(rel.Type == IMAGE_REL_MIPS_REFHI ||
           rel.Type == IMAGE_REL_MIPS_SECRELHI);
    ArrayRef<coff_relocation> relocs = sec.getRelocs();
    auto it = llvm::find_if(relocs, [&](const coff_relocation &candidate) {
      return &candidate == &rel;
    });
    if (it == relocs.end() || std::next(it) == relocs.end() ||
        std::next(it)->Type != IMAGE_REL_MIPS_PAIR) {
      error("MIPS HI16 relocation without matching PAIR relocation in " +
            toString(sec.file));
      return 0;
    }
    return SignExtend64<16>(std::next(it)->SymbolTableIndex & 0xffff);
  }

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    applyRelMIPS(ctx, *this, sec, off, rel, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &sec, const coff_relocation &rel,
             uint64_t targetRVA,
             std::optional<uint16_t> relocationValue) const override {
    return getBaserelMIPS(ctx, *this, sec, rel, targetRVA, relocationValue);
  }
};

class PPCTargetInfo final : public TargetInfo {
public:
  using TargetInfo::TargetInfo;

  bool isMetadataRelocation(const coff_relocation &rel) const override {
    uint16_t type = rel.Type & IMAGE_REL_PPC_TYPEMASK;
    return type == IMAGE_REL_PPC_PAIR || type == IMAGE_REL_PPC_IMGLUE;
  }

  int64_t getPairAddend(const SectionChunk &sec,
                        const coff_relocation &rel) const override {
    assert((rel.Type & IMAGE_REL_PPC_TYPEMASK) == IMAGE_REL_PPC_REFHI ||
           (rel.Type & IMAGE_REL_PPC_TYPEMASK) == IMAGE_REL_PPC_SECRELHI);
    ArrayRef<coff_relocation> relocs = sec.getRelocs();
    auto it = llvm::find_if(
        relocs, [&](const coff_relocation &r) { return &r == &rel; });
    if (it == relocs.end() || std::next(it) == relocs.end() ||
        std::next(it)->Type != IMAGE_REL_PPC_PAIR) {
      error("PowerPC HI16 relocation without matching PAIR relocation in " +
            toString(sec.file));
      return 0;
    }
    return SignExtend64<16>(std::next(it)->SymbolTableIndex & 0xffff);
  }

  void applyRelocation(const SectionChunk &sec, uint8_t *off,
                       const coff_relocation &rel, OutputSection *os,
                       uint64_t s, uint64_t p) const override {
    applyRelPPC(ctx, *this, sec, off, rel, os, s, p, ctx.config.imageBase);
  }

  std::optional<BaserelInfo>
  getBaserel(const SectionChunk &sec, const coff_relocation &rel,
             uint64_t targetRVA,
             std::optional<uint16_t> relocationValue) const override {
    return getBaserelPPC(ctx, *this, sec, rel, targetRVA, relocationValue);
  }
};

} // namespace

void TargetInfo::applyRelocation(const SectionChunk &sec, uint8_t *,
                                 const coff_relocation &rel, OutputSection *,
                                 uint64_t, uint64_t) const {
  error("unsupported relocation type 0x" + Twine::utohexstr(rel.Type) + " in " +
        toString(sec.file));
}

int64_t TargetInfo::getPairAddend(const SectionChunk &,
                                  const coff_relocation &) const {
  llvm_unreachable("target does not support paired relocations");
}

std::unique_ptr<TargetInfo> createTargetInfo(COFFLinkerContext &ctx,
                                             MachineTypes machine) {
  switch (machine) {
  case AMD64:
    return std::make_unique<AMD64TargetInfo>(ctx);
  case I386:
    return std::make_unique<I386TargetInfo>(ctx);
  case ALPHA:
  case IMAGE_FILE_MACHINE_ALPHA64:
    return std::make_unique<AlphaTargetInfo>(ctx);
  case ARMNT:
    return std::make_unique<ARMTargetInfo>(ctx);
  case ARM64:
  case ARM64EC:
  case ARM64X:
    return std::make_unique<ARM64TargetInfo>(ctx);
  case IMAGE_FILE_MACHINE_R4000:
    return std::make_unique<MIPSTargetInfo>(ctx);
  case IMAGE_FILE_MACHINE_POWERPC:
    return std::make_unique<PPCTargetInfo>(ctx);
  case IMAGE_FILE_MACHINE_RISCV32:
  case IMAGE_FILE_MACHINE_RISCV64:
    return std::make_unique<RISCVTargetInfo>(ctx);
  default:
    llvm_unreachable("unknown machine type");
  }
}

void applyMOV32T(uint8_t *off, uint32_t v) {
  uint16_t immW = readMOV(off, false);    // read MOVW operand
  uint16_t immT = readMOV(off + 4, true); // read MOVT operand
  uint32_t imm = immW | (immT << 16);
  v += imm;                   // add the immediate offset
  applyMOV(off, v);           // set MOVW operand
  applyMOV(off + 4, v >> 16); // set MOVT operand
}

void applyBranch24T(uint8_t *off, int32_t v) {
  if (!isInt<25>(v))
    error("relocation out of range");
  uint32_t s = v < 0 ? 1 : 0;
  uint32_t j1 = ((~v >> 23) & 1) ^ s;
  uint32_t j2 = ((~v >> 22) & 1) ^ s;
  or16(off, (s << 10) | ((v >> 12) & 0x3ff));
  // Clear out the J1 and J2 bits which may be set.
  write16le(off + 2, (read16le(off + 2) & 0xd000) | (j1 << 13) | (j2 << 11) |
                         ((v >> 1) & 0x7ff));
}

// Interpret the existing immediate value as a byte offset to the
// target symbol, then update the instruction with the immediate as
// the page offset from the current instruction to the target.
void applyArm64Addr(uint8_t *off, uint64_t s, uint64_t p, int shift) {
  uint32_t orig = read32le(off);
  int64_t imm =
      SignExtend64<21>(((orig >> 29) & 0x3) | ((orig >> 3) & 0x1FFFFC));
  s += imm;
  imm = (s >> shift) - (p >> shift);
  uint32_t immLo = (imm & 0x3) << 29;
  uint32_t immHi = (imm & 0x1FFFFC) << 3;
  uint64_t mask = (0x3 << 29) | (0x1FFFFC << 3);
  write32le(off, (orig & ~mask) | immLo | immHi);
}

// Update the immediate field in a AARCH64 ldr, str, and add instruction.
// Optionally limit the range of the written immediate by one or more bits
// (rangeLimit).
void applyArm64Imm(uint8_t *off, uint64_t imm, uint32_t rangeLimit) {
  uint32_t orig = read32le(off);
  imm += (orig >> 10) & 0xFFF;
  orig &= ~(0xFFF << 10);
  write32le(off, orig | ((imm & (0xFFF >> rangeLimit)) << 10));
}

// Add the 12 bit page offset to the existing immediate.
// Ldr/str instructions store the opcode immediate scaled
// by the load/store size (giving a larger range for larger
// loads/stores). The immediate is always (both before and after
// fixing up the relocation) stored scaled similarly.
// Even if larger loads/stores have a larger range, limit the
// effective offset to 12 bit, since it is intended to be a
// page offset.
void applyArm64Ldr(uint8_t *off, uint64_t imm) {
  uint32_t orig = read32le(off);
  uint32_t size = orig >> 30;
  // 0x04000000 indicates SIMD/FP registers
  // 0x00800000 indicates 128 bit
  if ((orig & 0x4800000) == 0x4800000)
    size += 4;
  if ((imm & ((1 << size) - 1)) != 0)
    error("misaligned ldr/str offset");
  applyArm64Imm(off, imm >> size, size);
}

void applyArm64Branch26(uint8_t *off, int64_t v) {
  if (!isInt<28>(v))
    error("relocation out of range");
  or32(off, (v & 0x0FFFFFFC) >> 2);
}

} // namespace lld::coff
