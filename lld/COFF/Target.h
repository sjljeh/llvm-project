//===- Target.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_COFF_TARGET_H
#define LLD_COFF_TARGET_H

#include "llvm/ADT/Twine.h"
#include "llvm/Object/COFF.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace lld::coff {

class COFFLinkerContext;
class Defined;
class OutputSection;
class SectionChunk;

struct BaserelInfo {
  uint32_t offset;
  uint8_t type;
  uint16_t extra = 0;
};

class TargetInfo {
public:
  explicit TargetInfo(COFFLinkerContext &ctx) : ctx(ctx) {}
  virtual ~TargetInfo() = default;

  virtual void applyRelocation(const SectionChunk &section, uint8_t *off,
                               const llvm::object::coff_relocation &rel,
                               OutputSection *os, uint64_t s, uint64_t p) const;

  // Metadata records do not refer to an ordinary symbol table entry.
  virtual bool
  isMetadataRelocation(const llvm::object::coff_relocation &rel) const {
    return false;
  }

  // Find the pair associated with rel and decode its addend.
  virtual int64_t getPairAddend(const SectionChunk &section,
                                const llvm::object::coff_relocation &rel) const;

  virtual std::optional<BaserelInfo>
  getBaserel(const SectionChunk &section,
             const llvm::object::coff_relocation &rel, uint64_t targetRVA,
             std::optional<uint16_t> relocationValue) const = 0;

protected:
  COFFLinkerContext &ctx;
};

std::unique_ptr<TargetInfo> createTargetInfo(COFFLinkerContext &ctx,
                                             llvm::COFF::MachineTypes machine);

void applyMOV32T(uint8_t *off, uint32_t v);
void applyBranch24T(uint8_t *off, int32_t v);
// The offset already encoded in a Thumb-2 branch relocated by type.
int32_t getARMBranchAddend(uint16_t type, const uint8_t *loc);

// Whether an ARMNT address of sym refers to Thumb code. Like link.exe, this
// follows the IMAGE_SCN_MEM_16BIT flag of the section defining sym; synthetic
// code (thunks) is Thumb, and absolute symbols never are.
bool isARMThumbTarget(Defined *sym);

// The address of sym as an ARMNT pointer, relocation target or entry point,
// with the Thumb bit set for Thumb code.
uint64_t getARMTargetRVA(Defined *sym);

void writePPC32(uint8_t *off, uint32_t v, bool isLE);

void applyArm64Addr(uint8_t *off, uint64_t s, uint64_t p, int shift);
void applyArm64Imm(uint8_t *off, uint64_t imm, uint32_t rangeLimit);
void applyArm64Ldr(uint8_t *off, uint64_t imm);
void applyArm64Branch26(uint8_t *off, int64_t v);
void applyRISCVPCRelPair(uint8_t *off, int64_t value, const llvm::Twine &context);
bool applyRISCVUType(uint8_t *off, int64_t value, const llvm::Twine &context);
void applyRISCVIType(uint8_t *off, int64_t value);

} // namespace lld::coff

#endif
