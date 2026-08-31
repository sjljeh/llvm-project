//===- Alpha.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class Alpha final : public TargetInfo {
public:
  explicit Alpha(Ctx &);
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
} // namespace

Alpha::Alpha(Ctx &ctx) : TargetInfo(ctx) {
  copyRel = R_ALPHA_COPY;
  gotRel = R_ALPHA_GLOB_DAT;
  pltRel = R_ALPHA_JMP_SLOT;
  relativeRel = R_ALPHA_RELATIVE;
  symbolicRel = R_ALPHA_REFQUAD;
  tlsGotRel = R_ALPHA_TPREL64;
  tlsModuleIndexRel = R_ALPHA_DTPMOD64;
  tlsOffsetRel = R_ALPHA_DTPREL64;

  gotEntrySize = 8;
  gotPltHeaderEntriesNum = 0;
  bool openbsd = ctx.arg.osabi == ELFOSABI_OPENBSD ||
                 StringRef(ctx.arg.emulation).starts_with("elf64alpha_obsd");
  defaultImageBase = openbsd ? 0x2000000 : 0x120000000;
  defaultCommonPageSize = 0x2000;
  defaultMaxPageSize = 0x10000;

  // call_pal 0x80 (bugchk) is used as a four-byte trap instruction.
  trapInstr = {0x80, 0x00, 0x00, 0x00};
}

RelExpr Alpha::getRelExpr(RelType type, const Symbol &s,
                          const uint8_t *loc) const {
  if (s.isGnuIFunc()) {
    Err(ctx) << "Alpha ELF IFUNC relocations are unsupported";
    return R_NONE;
  }
  switch (type) {
  case R_ALPHA_NONE:
    return R_NONE;
  case R_ALPHA_LITUSE:
    // Semantic marker consumed by Alpha link-time relaxations.  Until those
    // relaxations are implemented, retain it as a no-op relocation.
    return R_RELAX_HINT;
  case R_ALPHA_REFLONG:
  case R_ALPHA_REFQUAD:
    return R_ABS;
  case R_ALPHA_GPREL32:
  case R_ALPHA_GPRELHIGH:
  case R_ALPHA_GPRELLOW:
  case R_ALPHA_GPREL16:
    ctx.in.got->hasGotOffRel.store(true, std::memory_order_relaxed);
    return R_GOTREL;
  case R_ALPHA_LITERAL:
    return R_GOT_OFF;
  case R_ALPHA_GPDISP:
    ctx.in.got->hasGotOffRel.store(true, std::memory_order_relaxed);
    return R_GOTONLY_PC;
  case R_ALPHA_BRADDR:
  case R_ALPHA_HINT:
  case R_ALPHA_BRSGP:
  case R_ALPHA_SREL16:
  case R_ALPHA_SREL32:
  case R_ALPHA_SREL64:
    return R_PC;
  case R_ALPHA_DTPMOD64:
    return R_ABS;
  case R_ALPHA_DTPREL64:
  case R_ALPHA_DTPRELHI:
  case R_ALPHA_DTPRELLO:
  case R_ALPHA_DTPREL16:
    return R_DTPREL;
  case R_ALPHA_TPREL64:
  case R_ALPHA_TPRELHI:
  case R_ALPHA_TPRELLO:
  case R_ALPHA_TPREL16:
    return R_TPREL;
  case R_ALPHA_GOTTPREL:
    const_cast<Symbol &>(s).setFlags(NEEDS_TLSIE);
    return RE_ALPHA_GOT_OFF;
  case R_ALPHA_GOTDTPREL:
  case R_ALPHA_TLSGD:
  case R_ALPHA_TLSLDM:
    Err(ctx) << getErrorLoc(ctx, loc)
             << "Alpha GOT-based TLS relocation is not supported yet ("
             << type.v << ") against symbol " << &s;
    return R_NONE;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unknown Alpha relocation (" << type.v
             << ") against symbol " << &s;
    return R_NONE;
  }
}

RelType Alpha::getDynRel(RelType type) const {
  if (type == R_ALPHA_REFLONG || type == R_ALPHA_REFQUAD)
    return type;
  return R_ALPHA_NONE;
}

void Alpha::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  auto writeImm16 = [&](uint64_t v) {
    write32le(loc, (read32le(loc) & 0xffff0000) | (v & 0xffff));
  };
  auto writeHi16 = [&](int64_t v) { writeImm16((v + 0x8000) >> 16); };

  switch (rel.type) {
  case R_ALPHA_NONE:
  case R_ALPHA_LITUSE:
    return;
  case R_ALPHA_REFLONG:
    checkIntUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    return;
  case R_ALPHA_REFQUAD:
    write64le(loc, val);
    return;
  case R_ALPHA_GPREL32: {
    int64_t v = static_cast<int64_t>(val) - 0x8000;
    checkInt(ctx, loc, v, 32, rel);
    write32le(loc, v);
    return;
  }
  case R_ALPHA_LITERAL:
  case R_ALPHA_GOTTPREL: {
    if (rel.type == R_ALPHA_LITERAL && rel.addend != 0) {
      Err(ctx) << "R_ALPHA_LITERAL with a non-zero addend is unsupported";
      return;
    }
    int64_t v = static_cast<int64_t>(val) - 0x8000;
    checkInt(ctx, loc, v, 16, rel);
    writeImm16(v);
    return;
  }
  case R_ALPHA_GPDISP: {
    // The relocation addend is the distance from the ldah carrying the
    // relocation to its paired lda.  It is not part of the GP displacement.
    int64_t v = static_cast<int64_t>(val) - rel.addend + 0x8000;
    checkInt(ctx, loc, v, 32, rel);
    writeHi16(v);
    uint8_t *lo = loc + rel.addend;
    write32le(lo, (read32le(lo) & 0xffff0000) | (v & 0xffff));
    return;
  }
  case R_ALPHA_GPRELHIGH: {
    int64_t v = static_cast<int64_t>(val) - 0x8000;
    writeHi16(v);
    return;
  }
  case R_ALPHA_GPRELLOW: {
    int64_t v = static_cast<int64_t>(val) - 0x8000;
    writeImm16(v);
    return;
  }
  case R_ALPHA_GPREL16: {
    int64_t v = static_cast<int64_t>(val) - 0x8000;
    checkInt(ctx, loc, v, 16, rel);
    writeImm16(v);
    return;
  }
  case R_ALPHA_BRADDR:
  case R_ALPHA_BRSGP: {
    int64_t v = static_cast<int64_t>(val) - 4;
    if (rel.type == R_ALPHA_BRSGP && rel.sym &&
        (rel.sym->stOther & 0xfc) == ELF::STO_ALPHA_STD_GPLOAD)
      v += 8;
    checkAlignment(ctx, loc, v, 4, rel);
    int64_t disp = v >> 2;
    checkInt(ctx, loc, disp, 21, rel);
    write32le(loc, (read32le(loc) & 0xffe00000) | (disp & 0x1fffff));
    return;
  }
  case R_ALPHA_HINT: {
    int64_t v = static_cast<int64_t>(val) - 4;
    checkAlignment(ctx, loc, v, 4, rel);
    int64_t disp = v >> 2;
    checkInt(ctx, loc, disp, 14, rel);
    write32le(loc, (read32le(loc) & 0xffffc000) | (disp & 0x3fff));
    return;
  }
  case R_ALPHA_SREL16:
    checkInt(ctx, loc, val, 16, rel);
    write16le(loc, val);
    return;
  case R_ALPHA_SREL32:
    checkInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    return;
  case R_ALPHA_SREL64:
    write64le(loc, val);
    return;
  case R_ALPHA_DTPMOD64:
    write64le(loc, val);
    return;
  case R_ALPHA_DTPREL64:
  case R_ALPHA_TPREL64:
    write64le(loc, val);
    return;
  case R_ALPHA_DTPRELHI:
  case R_ALPHA_TPRELHI:
    writeHi16(val);
    return;
  case R_ALPHA_DTPRELLO:
  case R_ALPHA_TPRELLO:
    writeImm16(val);
    return;
  case R_ALPHA_DTPREL16:
  case R_ALPHA_TPREL16:
    checkInt(ctx, loc, val, 16, rel);
    writeImm16(val);
    return;
  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized Alpha relocation "
             << rel.type.v;
  }
}

void elf::setAlphaTargetInfo(Ctx &ctx) { ctx.target.reset(new Alpha(ctx)); }
