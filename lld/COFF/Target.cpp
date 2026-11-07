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

// Thumb-2 conditional branch (T3); the offset is S:J2:J1:imm6:imm11:'0'.
static void applyBranch20T(uint8_t *off, int32_t v) {
  if (!isInt<21>(v))
    error("relocation out of range");
  uint32_t s = v < 0 ? 1 : 0;
  uint32_t j1 = (v >> 18) & 1;
  uint32_t j2 = (v >> 19) & 1;
  write16le(off, (read16le(off) & 0xfbc0) | (s << 10) | ((v >> 12) & 0x3f));
  write16le(off + 2, (read16le(off + 2) & 0xd000) | (j1 << 13) | (j2 << 11) |
                         ((v >> 1) & 0x7ff));
}

static int32_t readBranch20T(const uint8_t *off) {
  uint16_t hw1 = read16le(off), hw2 = read16le(off + 2);
  uint32_t v = (((hw1 >> 10) & 1) << 20) | (((hw2 >> 11) & 1) << 19) |
               (((hw2 >> 13) & 1) << 18) | ((hw1 & 0x3f) << 12) |
               ((hw2 & 0x7ff) << 1);
  return SignExtend32<21>(v);
}

static int32_t readBranch24T(const uint8_t *off) {
  uint16_t hw1 = read16le(off), hw2 = read16le(off + 2);
  uint32_t s = (hw1 >> 10) & 1;
  uint32_t i1 = ~(((hw2 >> 13) & 1) ^ s) & 1;
  uint32_t i2 = ~(((hw2 >> 11) & 1) ^ s) & 1;
  uint32_t v = (s << 24) | (i1 << 23) | (i2 << 22) | ((hw1 & 0x3ff) << 12) |
               ((hw2 & 0x7ff) << 1);
  return SignExtend32<25>(v);
}

// link.exe ignores the offset a Thumb-2 branch already encodes: it ORs the
// displacement into S and the immediate fields and rewrites J1 and J2. This
// computes its result, to warn when it differs from adding the offset.
static void applyBranchLinkExe(uint8_t *off, int32_t v, bool cond) {
  uint32_t s = v < 0 ? 1 : 0;
  uint32_t j1 = cond ? (v >> 18) & 1 : ((~v >> 23) & 1) ^ s;
  uint32_t j2 = cond ? (v >> 19) & 1 : ((~v >> 22) & 1) ^ s;
  or16(off, (s << 10) | ((v >> 12) & (cond ? 0x3f : 0x3ff)));
  write16le(off + 2, (read16le(off + 2) & ~0x2800) | (j1 << 13) | (j2 << 11) |
                         ((v >> 1) & 0x7ff));
}


// ARM MOVW/MOVT pair (A2 encodings): imm4 in bits 19:16, imm12 in 11:0. The
// pair's immediate is an addend.
static void applyMOV32A(uint8_t *off, uint32_t v) {
  auto get = [](uint32_t w) { return ((w >> 4) & 0xf000) | (w & 0xfff); };
  auto set = [](uint32_t w, uint32_t imm) {
    return (w & 0xfff0f000) | ((imm & 0xf000) << 4) | (imm & 0xfff);
  };
  uint32_t lo = read32le(off), hi = read32le(off + 4);
  v += get(lo) | (get(hi) << 16);
  write32le(off, set(lo, v & 0xffff));
  write32le(off + 4, set(hi, v >> 16));
}

static StringRef getARMRelocationName(uint16_t type) {
  switch (type) {
  case IMAGE_REL_ARM_ADDR32NB:
    return "IMAGE_REL_ARM_ADDR32NB";
  case IMAGE_REL_ARM_REL32:
    return "IMAGE_REL_ARM_REL32";
  case IMAGE_REL_ARM_BRANCH20T:
    return "IMAGE_REL_ARM_BRANCH20T";
  case IMAGE_REL_ARM_BRANCH24T:
    return "IMAGE_REL_ARM_BRANCH24T";
  case IMAGE_REL_ARM_BLX23T:
    return "IMAGE_REL_ARM_BLX23T";
  case IMAGE_REL_ARM_BRANCH24:
    return "IMAGE_REL_ARM_BRANCH24";
  case IMAGE_REL_ARM_BLX24:
    return "IMAGE_REL_ARM_BLX24";
  case IMAGE_REL_ARM_BRANCH11:
    return "IMAGE_REL_ARM_BRANCH11";
  case IMAGE_REL_ARM_BLX11:
    return "IMAGE_REL_ARM_BLX11";
  default:
    return "ARM";
  }
}

static void applyRelARM(COFFLinkerContext &ctx, const SectionChunk &sec,
                        uint8_t *off, const coff_relocation &rel,
                        OutputSection *os, uint64_t s, uint64_t p,
                        uint64_t imageBase) {
  auto *sym = dyn_cast_or_null<Defined>(sec.file->getSymbol(rel.SymbolTableIndex));
  // Addresses of Thumb code carry the Thumb bit; link.exe adds it to every
  // relocation against a section marked IMAGE_SCN_MEM_16BIT, including SECREL
  // outside of debug info.
  bool thumb = sym && isARMThumbTarget(sym);
  uint64_t sx = thumb ? s | 1 : s;
  // link.exe adds the bit even to an odd address, which yields neither the
  // address nor a Thumb pointer; keep the address and warn.
  if (thumb && (s & 1) && rel.Type != IMAGE_REL_ARM_SECTION &&
      !sec.isCodeView())
    warn(toString(sec.file) + ": relocation against '" + toString(ctx, *sym) +
         "', at an odd address in a Thumb (IMAGE_SCN_MEM_16BIT) section; "
         "link.exe would add the Thumb bit to the address");

  // link.exe rejects (LNK2016) absolute targets of relocations that are
  // relative to the image or to the relocation itself.
  auto isAbsoluteTarget = [&]() {
    if (!sym || !isa<DefinedAbsolute>(sym) || sec.isCodeView())
      return false;
    error(toString(sec.file) + ": absolute symbol '" + toString(ctx, *sym) +
          "' cannot be the target of an " + getARMRelocationName(rel.Type) +
          " relocation");
    return true;
  };
  // A branch that cannot switch instruction set targets code in the other
  // one. Returns whether to encode the branch anyway, as link.exe does with
  // /OPT:NOREF.
  auto reportStateMismatch = [&](StringRef what) {
    std::string msg = toString(sec.file) + ": " +
                      getARMRelocationName(rel.Type).str() +
                      " relocation targets '" + toString(ctx, *sym) +
                      "', which is " + what.str() +
                      " (IMAGE_SCN_MEM_16BIT) section";
    if (ctx.config.doGC) {
      error(msg);
      return false;
    }
    warn(msg + "; the branch will not change instruction set");
    return true;
  };
  auto checkThumbBranch = [&]() {
    if (isAbsoluteTarget())
      return false;
    // A B.W or Bcc.W cannot change instruction set. link.exe diagnoses this
    // (LNK2013) with /OPT:REF but encodes the branch as is with /OPT:NOREF,
    // which works if the target is Thumb code in a section that lacks the flag.
    if (thumb || !sym)
      return true;
    return reportStateMismatch("not in a Thumb");
  };
  auto warnARMState = [&](StringRef from, StringRef to, StringRef state) {
    warn(toString(sec.file) + ": " + getARMRelocationName(rel.Type) +
         " relocation against '" + (sym ? toString(ctx, *sym) : "") +
         "': changed " + from + " to " + to + " to stay in " + state +
         " state; link.exe would keep the " + from);
  };
  auto warnAddend = [&](int64_t addend) {
    warn(toString(sec.file) + ": " + getARMRelocationName(rel.Type) +
         " relocation against '" + (sym ? toString(ctx, *sym) : "") +
         "' adds the offset " + Twine(addend) +
         " encoded in the instruction; link.exe would merge it differently "
         "and branch elsewhere");
  };
  // ARM B/BL/BLX (A1/A2). link.exe ORs the displacement into the immediate.
  auto applyARMBranch = [&](uint64_t target, uint64_t pc, bool toBLX) {
    uint32_t insn = read32le(off);
    bool isBLX = (insn >> 28) == 0xf;
    int64_t addend = SignExtend64<26>((insn & 0xffffff) << 2) +
                     (isBLX ? ((insn >> 23) & 2) : 0);
    int64_t v = int64_t(target) - int64_t(pc);
    int64_t w = v + addend;
    if (!isInt<26>(w))
      error(toString(sec.file) + ": " + getARMRelocationName(rel.Type) +
            " relocation out of range");
    uint32_t res;
    if (toBLX)
      res = 0xfa000000 | ((w & 2) << 23) | ((w >> 2) & 0xffffff);
    else
      res = (insn & 0xff000000) | ((w >> 2) & 0xffffff);
    write32le(off, res);
    if (addend) {
      uint32_t ms = insn | ((v >> 2) & 0xffffff);
      if (toBLX)
        ms = (ms & 0x00ffffff) | 0xfa000000 | ((v & 2) << 23);
      if (ms != res)
        warnAddend(addend);
    }
  };
  // Thumb-1 BL/BLX pair: 11110 offset[22:12], 11111/11101 offset[11:1].
  auto applyThumb1Branch = [&](uint64_t target, uint64_t pc) {
    uint16_t hi = read16le(off), lo = read16le(off + 2);
    int64_t addend =
        SignExtend64<23>(((hi & 0x7ff) << 12) | ((lo & 0x7ff) << 1));
    int64_t v = int64_t(target) - int64_t(pc);
    int64_t w = v + addend;
    if (!isInt<23>(w))
      error(toString(sec.file) + ": " + getARMRelocationName(rel.Type) +
            " relocation out of range");
    write16le(off, (hi & 0xf800) | ((w >> 12) & 0x7ff));
    write16le(off + 2, (lo & 0xf800) | ((w >> 1) & 0x7ff));
    if (addend && ((((v >> 12) & 0x7ff) | hi) != read16le(off) ||
                   (((v >> 1) & 0x7ff) | lo) != read16le(off + 2)))
      warnAddend(addend);
  };
  // Apply a branch to target from pc (the base of the PC-relative offset).
  // Any offset the instruction already encodes is added. link.exe instead
  // merges the two bitwise; warn where that would branch elsewhere.
  auto applyBranch = [&](uint64_t target, uint64_t pc, bool toARM) {
    bool cond = rel.Type == IMAGE_REL_ARM_BRANCH20T;
    uint8_t orig[4];
    memcpy(orig, off, 4);
    int32_t addend = rel.Type == IMAGE_REL_ARM_BRANCH20T ? readBranch20T(off)
                                                         : readBranch24T(off);
    int64_t v = int64_t(target) - int64_t(pc) + addend;
    if (cond)
      applyBranch20T(off, v);
    else
      applyBranch24T(off, v);
    if (toARM)
      write16le(off + 2, read16le(off + 2) & ~0x1000);
    if (addend == 0)
      return;
    uint8_t linkExe[4];
    memcpy(linkExe, orig, 4);
    applyBranchLinkExe(linkExe, int64_t(target) - int64_t(pc), cond);
    if (toARM)
      write16le(linkExe + 2, read16le(linkExe + 2) & ~0x1000);
    if (memcmp(linkExe, off, 4))
      warnAddend(addend);
  };

  switch (rel.Type) {
  case IMAGE_REL_ARM_ABSOLUTE:
    break;
  case IMAGE_REL_ARM_ADDR32:
    add32(off, sx + imageBase);
    break;
  case IMAGE_REL_ARM_ADDR32NB:
    if (!isAbsoluteTarget())
      add32(off, sx);
    break;
  case IMAGE_REL_ARM_MOV32T:
    applyMOV32T(off, sx + imageBase);
    break;
  case IMAGE_REL_ARM_BRANCH20T:
  case IMAGE_REL_ARM_BRANCH24T:
    if (checkThumbBranch())
      applyBranch(sx, p + 4, false);
    break;
  case IMAGE_REL_ARM_BLX23T: {
    if (isAbsoluteTarget())
      break;
    uint16_t hw2 = read16le(off + 2);
    if (thumb || !sym) {
      applyBranch(sx, p + 4, false);
      // A BLX would switch to ARM state; link.exe leaves it in place.
      if (!(hw2 & 0x1000)) {
        write16le(off + 2, read16le(off + 2) | 0x1000);
        warnARMState("BLX", "BL", "Thumb");
      }
      break;
    }
    // A call to ARM code becomes BLX, which branches relative to Align(PC, 4)
    // and needs a word-aligned target (link.exe: LNK2008).
    if (s & 3) {
      error(toString(sec.file) + ": IMAGE_REL_ARM_BLX23T relocation target '" +
            toString(ctx, *sym) + "' is not 4-byte aligned");
      break;
    }
    // Only a BL can become a BLX (link.exe corrupts a B.W instead).
    if (!(hw2 & 0x4000)) {
      error(toString(sec.file) + ": IMAGE_REL_ARM_BLX23T relocation against '" +
            toString(ctx, *sym) +
            "' is not on a BL or BLX instruction and cannot reach ARM code");
      break;
    }
    applyBranch(s, (p + 4) & ~uint64_t(3), true);
    break;
  }
  case IMAGE_REL_ARM_MOV32A:
    applyMOV32A(off, sx + imageBase);
    break;
  case IMAGE_REL_ARM_BRANCH24:
  case IMAGE_REL_ARM_BLX24: {
    // ARM B, BL or BLX, relative to PC + 8.
    if (isAbsoluteTarget() || !sym)
      break;
    bool isBLX = (read32le(off) >> 28) == 0xf;
    if (thumb && rel.Type == IMAGE_REL_ARM_BRANCH24) {
      // link.exe does not turn these into BLX (LNK2013).
      if (reportStateMismatch("in a Thumb"))
        applyARMBranch(s, p + 8, false);
      break;
    }
    if (thumb) {
      // BLX24 to Thumb code: BLX, with H selecting the halfword.
      applyARMBranch(sx, p + 8, true);
      break;
    }
    if (s & 3) {
      error(toString(sec.file) + ": " + getARMRelocationName(rel.Type) +
            " relocation target '" + toString(ctx, *sym) +
            "' is not 4-byte aligned");
      break;
    }
    applyARMBranch(s, p + 8, false);
    if (isBLX) {
      // A BLX would switch to Thumb state; link.exe leaves it in place.
      write32le(off, (read32le(off) & 0x00ffffff) | 0xeb000000);
      warnARMState("BLX", "BL", "ARM");
    }
    break;
  }
  case IMAGE_REL_ARM_BRANCH11:
  case IMAGE_REL_ARM_BLX11: {
    // Thumb-1 BL or BLX pair, relative to PC + 4.
    if (isAbsoluteTarget() || !sym)
      break;
    uint16_t lo = read16le(off + 2);
    if (thumb) {
      applyThumb1Branch(sx, p + 4);
      if (!(lo & 0x1000)) {
        write16le(off + 2, read16le(off + 2) | 0x1000);
        warnARMState("BLX", "BL", "Thumb");
      }
      break;
    }
    if (rel.Type == IMAGE_REL_ARM_BRANCH11) {
      // link.exe does not turn these into BLX (LNK2013).
      if (reportStateMismatch("not in a Thumb"))
        applyThumb1Branch(s, p + 4);
      break;
    }
    if (s & 3) {
      error(toString(sec.file) + ": IMAGE_REL_ARM_BLX11 relocation target '" +
            toString(ctx, *sym) + "' is not 4-byte aligned");
      break;
    }
    applyThumb1Branch(s, (p + 4) & ~uint64_t(3));
    write16le(off + 2, read16le(off + 2) & ~0x1000);
    break;
  }
  case IMAGE_REL_ARM_SECTION:
    applySecIdx(off, os, ctx.outputSections.size());
    break;
  case IMAGE_REL_ARM_SECREL:
    // CodeView symbol and line addresses don't carry the Thumb bit.
    applySecRel(&sec, off, os, sec.isCodeView() ? s : sx);
    break;
  case IMAGE_REL_ARM_REL32:
    if (!isAbsoluteTarget())
      add32(off, sx - p - 4);
    break;
  default:
    error("unsupported relocation type 0x" + Twine::utohexstr(rel.Type) +
          " in " + toString(sec.file));
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
  // A branch can only reach a word-aligned target; link.exe rejects others
  // (LNK2008) rather than branch beside them.
  if ((type == IMAGE_REL_ARM64_BRANCH26 || type == IMAGE_REL_ARM64_BRANCH19 ||
       type == IMAGE_REL_ARM64_BRANCH14) &&
      ((s - p) & 3)) {
    error(toString(sec.file) + ": branch relocation target at 0x" +
          Twine::utohexstr(s) + " is not 4-byte aligned");
    return;
  }
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

void applyRISCVPCRelPair(uint8_t *off, int64_t value, const Twine &context) {
  if (applyRISCVUType(off, value, context))
    applyRISCVIType(off + 4, value);
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

// A RISC-V HI20/LO12 pair carries its addend split across the two
// instructions: the high half holds the upper 20 bits and the low half the
// lower 12 bits of the same value. Either half therefore needs its partner
// before the full addend, and thus the relocated value, is known.
struct RISCVSplitReloc {
  const coff_relocation *high = nullptr;
  const coff_relocation *low = nullptr;
};

// Return the symbol of rel if it is a label defined inside sec, which is how
// a PC-relative low half names its AUIPC.
static const DefinedRegular *getRISCVLocalLabel(const SectionChunk *sec,
                                                const coff_relocation &rel) {
  auto *label = dyn_cast_or_null<DefinedRegular>(sec->file->getSymbol(rel.SymbolTableIndex));
  if (label && label->getChunk() == sec)
    return label;
  return nullptr;
}

// A PC-relative low half references the label of its AUIPC, so the pair is
// identified by address: the high half is the PCREL_HI20 record at the label's
// offset, and the low half is a PCREL_LO12 record whose label resolves to the
// high half's offset. Search outward from rel, as the partner is normally
// adjacent.
static std::optional<RISCVSplitReloc>
findRISCVPCRelPair(const SectionChunk *sec, const coff_relocation &rel) {
  ArrayRef<coff_relocation> relocs = sec->getRelocs();
  size_t index = &rel - relocs.data();
  RISCVSplitReloc pair;

  if (rel.Type == IMAGE_REL_RISCV_PCREL_HI20) {
    pair.high = &rel;
    auto isLow = [&](const coff_relocation &r) {
      if (!isRISCVLowType(r.Type, IMAGE_REL_RISCV_PCREL_LO12_I, IMAGE_REL_RISCV_PCREL_LO12_S))
        return false;
      const DefinedRegular *label = getRISCVLocalLabel(sec, r);
      return label && label->getValue() == rel.VirtualAddress;
    };
    for (size_t i = index + 1; i < relocs.size() && !pair.low; ++i)
      if (isLow(relocs[i]))
        pair.low = &relocs[i];
    for (size_t i = index; i > 0 && !pair.low; --i)
      if (isLow(relocs[i - 1]))
        pair.low = &relocs[i - 1];
    if (!pair.low) {
      error("RISC-V PCREL_HI20 relocation has no PCREL_LO12 relocation referencing its AUIPC in " + toString(sec->file));
      return std::nullopt;
    }
    return pair;
  }

  pair.low = &rel;
  const DefinedRegular *label = getRISCVLocalLabel(sec, rel);
  if (!label) {
    error("RISC-V PCREL_LO12 relocation does not reference a label in its own section in " + toString(sec->file));
    return std::nullopt;
  }
  uint32_t highOffset = label->getValue();
  auto isHigh = [&](const coff_relocation &r) {
    return r.Type == IMAGE_REL_RISCV_PCREL_HI20 && r.VirtualAddress == highOffset;
  };
  for (size_t i = index; i > 0 && !pair.high; --i)
    if (isHigh(relocs[i - 1]))
      pair.high = &relocs[i - 1];
  for (size_t i = index + 1; i < relocs.size() && !pair.high; ++i)
    if (isHigh(relocs[i]))
      pair.high = &relocs[i];
  if (!pair.high) {
    error("RISC-V PCREL_LO12 relocation references a label without a PCREL_HI20 relocation in " + toString(sec->file));
    return std::nullopt;
  }
  return pair;
}

// Absolute HI20/LO12 pairs have no label tying them together: both halves
// reference the target symbol itself. A low half pairs with the nearest
// preceding high half for the same symbol, matching the emission order of a
// LUI/ADDI sequence, and a high half owns the low halves that follow it up
// to the next high half for that symbol.
static std::optional<RISCVSplitReloc>
findRISCVAbsPair(const SectionChunk *sec, const coff_relocation &rel) {
  ArrayRef<coff_relocation> relocs = sec->getRelocs();
  size_t index = &rel - relocs.data();
  RISCVSplitReloc pair;

  if (rel.Type == IMAGE_REL_RISCV_HI20) {
    pair.high = &rel;
    for (size_t i = index + 1; i < relocs.size(); ++i) {
      const coff_relocation &r = relocs[i];
      if (r.SymbolTableIndex != rel.SymbolTableIndex)
        continue;
      if (r.Type == IMAGE_REL_RISCV_HI20)
        break;
      if (isRISCVLowType(r.Type, IMAGE_REL_RISCV_LO12_I, IMAGE_REL_RISCV_LO12_S)) {
        pair.low = &r;
        break;
      }
    }
  } else {
    pair.low = &rel;
    for (size_t i = index; i > 0; --i) {
      const coff_relocation &r = relocs[i - 1];
      if (r.Type == IMAGE_REL_RISCV_HI20 && r.SymbolTableIndex == rel.SymbolTableIndex) {
        pair.high = &r;
        break;
      }
    }
  }
  if (!pair.high || !pair.low) {
    error("RISC-V HI20/LO12 relocation has no matching partner for the same symbol in " + toString(sec->file));
    return std::nullopt;
  }
  return pair;
}

// Reconstruct the addend that MC split across the high and low instructions.
static std::optional<int64_t>
readRISCVSplitAddend(const SectionChunk *sec, const RISCVSplitReloc &pair,
                     uint16_t lowIType) {
  ArrayRef<uint8_t> data = sec->getContents();
  auto inBounds = [&](const coff_relocation *r) {
    return r->VirtualAddress <= data.size() && data.size() - r->VirtualAddress >= 4;
  };
  if (!inBounds(pair.high) || !inBounds(pair.low)) {
    error("RISC-V split relocation points beyond its input section in " + toString(sec->file));
    return std::nullopt;
  }

  uint32_t highInst = read32le(data.data() + pair.high->VirtualAddress);
  uint32_t expectedOpcode = pair.high->Type == IMAGE_REL_RISCV_PCREL_HI20 ? 0x17 : 0x37;
  if ((highInst & 0x7f) != expectedOpcode) {
    error("RISC-V split relocation does not reference the expected high instruction in " + toString(sec->file));
    return std::nullopt;
  }

  const uint8_t *lowInst = data.data() + pair.low->VirtualAddress;
  int64_t lowAddend = pair.low->Type == lowIType ? readRISCVIType(lowInst) : readRISCVSType(lowInst);
  return readRISCVUType(data.data() + pair.high->VirtualAddress) + lowAddend;
}

static void applyRISCVSplitHalf(uint8_t *off, uint16_t type, uint16_t highType,
                                uint16_t lowIType, int64_t value,
                                const Twine &context) {
  if (type == highType)
    applyRISCVUType(off, value, context);
  else if (type == lowIType)
    applyRISCVIType(off, value);
  else
    applyRISCVSType(off, value);
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
      uint32_t auipc = read32le(off);
      uint32_t jalr = read32le(off + 4);
      if ((auipc & 0x7f) != 0x17 || (jalr & 0x707f) != 0x67 || ((auipc >> 7) & 0x1f) != ((jalr >> 15) & 0x1f)) {
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
    std::optional<RISCVSplitReloc> pair = findRISCVPCRelPair(sec, rel);
    if (!pair)
      break;
    std::optional<int64_t> addend = readRISCVSplitAddend(sec, *pair, IMAGE_REL_RISCV_PCREL_LO12_I);
    if (!addend)
      break;
    // The high half references the target; the low half references the AUIPC
    // label, so its target is the high half's symbol. A high half whose
    // symbol could not be resolved has already been diagnosed.
    uint64_t target = s;
    if (rel.Type != IMAGE_REL_RISCV_PCREL_HI20) {
      auto *sym = dyn_cast_or_null<Defined>(sec->file->getSymbol(pair->high->SymbolTableIndex));
      if (!sym)
        break;
      target = sym->getRVA();
    }
    int64_t value = int64_t(target) + *addend - int64_t(sec->getRVA() + pair->high->VirtualAddress);
    applyRISCVSplitHalf(off, rel.Type, IMAGE_REL_RISCV_PCREL_HI20, IMAGE_REL_RISCV_PCREL_LO12_I, value, toString(sec->file));
    break;
  }
  case IMAGE_REL_RISCV_HI20:
  case IMAGE_REL_RISCV_LO12_I:
  case IMAGE_REL_RISCV_LO12_S: {
    if (!requireBytes(4))
      break;
    std::optional<RISCVSplitReloc> pair = findRISCVAbsPair(sec, rel);
    if (!pair)
      break;
    std::optional<int64_t> addend = readRISCVSplitAddend(sec, *pair, IMAGE_REL_RISCV_LO12_I);
    if (!addend)
      break;
    int64_t value = int64_t(imageBase) + int64_t(s) + *addend;
    applyRISCVSplitHalf(off, rel.Type, IMAGE_REL_RISCV_HI20, IMAGE_REL_RISCV_LO12_I, value, toString(sec->file));
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
    if (requireBytes(2)) {
      if ((read16le(off) & 0xe003) != 0xa001) {
        error("RISC-V RVC_JUMP relocation does not reference a C.J instruction");
        break;
      }
      applyRISCVRVCJump(off, int64_t(s) + readRISCVRVCJump(off) - int64_t(p));
    }
    break;
  case IMAGE_REL_RISCV_RVC_BRANCH:
    if (requireBytes(2)) {
      if ((read16le(off) & 0xc003) != 0xc001) {
        error("RISC-V RVC_BRANCH relocation does not reference a C.BEQZ or C.BNEZ instruction");
        break;
      }
      applyRISCVRVCBranch(off, int64_t(s) + readRISCVRVCBranch(off) - int64_t(p));
    }
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
  case IMAGE_REL_ARM_MOV32A:
    return makeBaserel(rel, IMAGE_REL_BASED_ARM_MOV32A);
  default:
    return std::nullopt;
  }
}

static std::optional<BaserelInfo> getBaserelARM64(const coff_relocation &rel) {
  if (rel.Type == IMAGE_REL_ARM64_ADDR64)
    return makeBaserel(rel, IMAGE_REL_BASED_DIR64);
  // An absolute 32-bit address needs a fixup as much as a 64-bit one; link.exe
  // emits HIGHLOW for it.
  if (rel.Type == IMAGE_REL_ARM64_ADDR32)
    return makeBaserel(rel, IMAGE_REL_BASED_HIGHLOW);
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
    applyRelARM(ctx, sec, off, rel, os, s, p, ctx.config.imageBase);
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

// Thumb-2 B.W, BL or BLX (T4/T1/T2); the offset is S:I1:I2:imm10:imm11:'0'
// with J1 = NOT(I1) XOR S and J2 = NOT(I2) XOR S. Any offset the instruction
// encodes is replaced.
void applyBranch24T(uint8_t *off, int32_t v) {
  if (!isInt<25>(v))
    error("relocation out of range");
  uint32_t s = v < 0 ? 1 : 0;
  uint32_t j1 = ((~v >> 23) & 1) ^ s;
  uint32_t j2 = ((~v >> 22) & 1) ^ s;
  write16le(off, (read16le(off) & 0xf800) | (s << 10) | ((v >> 12) & 0x3ff));
  write16le(off + 2, (read16le(off + 2) & 0xd000) | (j1 << 13) | (j2 << 11) |
                         ((v >> 1) & 0x7ff));
}

int32_t getARMBranchAddend(uint16_t type, const uint8_t *loc) {
  switch (type) {
  case IMAGE_REL_ARM_BRANCH20T:
    return readBranch20T(loc);
  case IMAGE_REL_ARM_BRANCH24T:
  case IMAGE_REL_ARM_BLX23T:
    return readBranch24T(loc);
  default:
    return 0;
  }
}

bool isARMThumbTarget(Defined *sym) {
  Chunk *c = sym->getChunk();
  if (!c)
    return false;
  if (auto *sc = dyn_cast<SectionChunk>(c))
    return sc->header->Characteristics & IMAGE_SCN_MEM_16BIT;
  return c->getOutputCharacteristics() & IMAGE_SCN_MEM_EXECUTE;
}

uint64_t getARMTargetRVA(Defined *sym) {
  return sym->getRVA() | (isARMThumbTarget(sym) ? 1 : 0);
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
