//===- Target.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_TARGET_H
#define LLD_COFF_TARGET_H

#include "llvm/Object/COFF.h"
#include <cstdint>
#include <memory>

namespace lld::coff {

class COFFLinkerContext;
class OutputSection;
class SectionChunk;

class TargetInfo {
public:
  explicit TargetInfo(COFFLinkerContext &ctx) : ctx(ctx) {}
  virtual ~TargetInfo() = default;

  virtual void applyRelocation(const SectionChunk &section, uint8_t *off,
                               const llvm::object::coff_relocation &rel,
                               OutputSection *os, uint64_t s, uint64_t p) const;

protected:
  COFFLinkerContext &ctx;
};

std::unique_ptr<TargetInfo> createTargetInfo(COFFLinkerContext &ctx,
                                             llvm::COFF::MachineTypes machine);

void applyMOV32T(uint8_t *off, uint32_t v);
void applyBranch24T(uint8_t *off, int32_t v);

void writePPC32(uint8_t *off, uint32_t v, bool isLE);

void applyArm64Addr(uint8_t *off, uint64_t s, uint64_t p, int shift);
void applyArm64Imm(uint8_t *off, uint64_t imm, uint32_t rangeLimit);
void applyArm64Ldr(uint8_t *off, uint64_t imm);
void applyArm64Branch26(uint8_t *off, int64_t v);

} // namespace lld::coff

#endif
