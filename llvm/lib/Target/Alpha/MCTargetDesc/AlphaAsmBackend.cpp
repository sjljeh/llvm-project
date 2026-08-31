//===-- AlphaAsmBackend.cpp - Alpha Assembler Backend ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaMCTargetDesc.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

class AlphaAsmBackend : public MCAsmBackend {
  Triple TT;
  bool IsTASO;

public:
  AlphaAsmBackend(const Triple &TT, bool IsTASO)
      : MCAsmBackend(llvm::endianness::little), TT(TT), IsTASO(IsTASO) {}

  MCFixupKindInfo getFixupKindInfo(MCFixupKind Kind) const override {
    static const MCFixupKindInfo Infos[Alpha::NumTargetFixupKinds -
                                       FirstTargetFixupKind] = {
        {"fixup_Alpha_Branch", 0, 21, 0},   {"fixup_Alpha_REFHI", 0, 16, 0},
        {"fixup_Alpha_REFLO", 0, 16, 0},    {"fixup_Alpha_LITERAL", 0, 16, 0},
        {"fixup_Alpha_LITUSE", 0, 0, 0},    {"fixup_Alpha_GPDISP", 0, 0, 0},
        {"fixup_Alpha_BRSGP", 0, 21, 0},    {"fixup_Alpha_HINT", 0, 14, 0},
        {"fixup_Alpha_GPREL16", 0, 16, 0},  {"fixup_Alpha_TLSGD", 0, 16, 0},
        {"fixup_Alpha_TLSLDM", 0, 16, 0},   {"fixup_Alpha_GOTDTPREL", 0, 16, 0},
        {"fixup_Alpha_DTPRELHI", 0, 16, 0}, {"fixup_Alpha_DTPRELLO", 0, 16, 0},
        {"fixup_Alpha_DTPREL16", 0, 16, 0}, {"fixup_Alpha_GOTTPREL", 0, 16, 0},
        {"fixup_Alpha_TPRELHI", 0, 16, 0},  {"fixup_Alpha_TPRELLO", 0, 16, 0},
        {"fixup_Alpha_TPREL16", 0, 16, 0},
    };
    if (Kind >= FirstTargetFixupKind && Kind < Alpha::NumTargetFixupKinds)
      return Infos[Kind - FirstTargetFixupKind];
    return MCAsmBackend::getFixupKindInfo(Kind);
  }

  void applyFixup(const MCFragment &F, const MCFixup &Fixup,
                  const MCValue &Target, uint8_t *Data, uint64_t Value,
                  bool IsResolved) override {
    MCFixupKind Kind = Fixup.getKind();
    if (TT.isOSBinFormatELF()) {
      switch (Kind) {
      case Alpha::fixup_Alpha_REFHI:
      case Alpha::fixup_Alpha_REFLO:
      case Alpha::fixup_Alpha_LITERAL:
      case Alpha::fixup_Alpha_LITUSE:
      case Alpha::fixup_Alpha_GPDISP:
      case Alpha::fixup_Alpha_BRSGP:
      case Alpha::fixup_Alpha_HINT:
      case Alpha::fixup_Alpha_GPREL16:
      case Alpha::fixup_Alpha_TLSGD:
      case Alpha::fixup_Alpha_TLSLDM:
      case Alpha::fixup_Alpha_GOTDTPREL:
      case Alpha::fixup_Alpha_DTPRELHI:
      case Alpha::fixup_Alpha_DTPRELLO:
      case Alpha::fixup_Alpha_DTPREL16:
      case Alpha::fixup_Alpha_GOTTPREL:
      case Alpha::fixup_Alpha_TPRELHI:
      case Alpha::fixup_Alpha_TPRELLO:
      case Alpha::fixup_Alpha_TPREL16:
        IsResolved = false;
        break;
      default:
        break;
      }
    }
    maybeAddReloc(F, Fixup, Target, Value, IsResolved);
    if (TT.isOSBinFormatELF() && !IsResolved && Kind >= FirstTargetFixupKind)
      return;

    switch (Kind) {
    default:
      return;
    case FK_Data_1:
      *Data = static_cast<uint8_t>(Value);
      return;
    case FK_Data_2:
      support::endian::write<uint16_t>(Data, Value, Endian);
      return;
    case FK_Data_4:
      support::endian::write<uint32_t>(Data, Value, Endian);
      return;
    case FK_Data_8:
      support::endian::write<uint64_t>(Data, Value, Endian);
      return;
    case Alpha::fixup_Alpha_Branch: {
      uint32_t Inst = support::endian::read32le(Data);
      int64_t Disp = static_cast<int64_t>(Value);
      if (IsResolved)
        Disp -= 4;
      Disp >>= 2;
      support::endian::write32le(Data,
                                 (Inst & 0xffe00000) |
                                     (static_cast<uint32_t>(Disp) & 0x1fffff));
      return;
    }
    case Alpha::fixup_Alpha_REFHI: {
      uint32_t Inst = support::endian::read32le(Data);
      support::endian::write32le(
          Data, (Inst & 0xffff0000) |
                    ((static_cast<uint32_t>(Value >> 16)) & 0xffff));
      return;
    }
    case Alpha::fixup_Alpha_REFLO: {
      uint32_t Inst = support::endian::read32le(Data);
      support::endian::write32le(
          Data, (Inst & 0xffff0000) | (static_cast<uint32_t>(Value) & 0xffff));
      return;
    }
    case Alpha::fixup_Alpha_LITERAL:
    case Alpha::fixup_Alpha_GPREL16:
    case Alpha::fixup_Alpha_TLSGD:
    case Alpha::fixup_Alpha_TLSLDM:
    case Alpha::fixup_Alpha_GOTDTPREL:
    case Alpha::fixup_Alpha_DTPRELHI:
    case Alpha::fixup_Alpha_DTPRELLO:
    case Alpha::fixup_Alpha_DTPREL16:
    case Alpha::fixup_Alpha_GOTTPREL:
    case Alpha::fixup_Alpha_TPRELHI:
    case Alpha::fixup_Alpha_TPRELLO:
    case Alpha::fixup_Alpha_TPREL16: {
      uint32_t Inst = support::endian::read32le(Data);
      support::endian::write32le(
          Data, (Inst & 0xffff0000) | (static_cast<uint32_t>(Value) & 0xffff));
      return;
    }
    case Alpha::fixup_Alpha_LITUSE:
    case Alpha::fixup_Alpha_GPDISP:
      return;
    case Alpha::fixup_Alpha_BRSGP: {
      uint32_t Inst = support::endian::read32le(Data);
      int64_t Disp = static_cast<int64_t>(Value);
      if (IsResolved)
        Disp -= 4;
      Disp >>= 2;
      support::endian::write32le(
          Data, (Inst & 0xffe00000) | (static_cast<uint32_t>(Disp) & 0x1fffff));
      return;
    }
    case Alpha::fixup_Alpha_HINT: {
      uint32_t Inst = support::endian::read32le(Data);
      int64_t Disp = static_cast<int64_t>(Value);
      if (IsResolved)
        Disp -= 4;
      Disp >>= 2;
      support::endian::write32le(
          Data, (Inst & 0xffffc000) | (static_cast<uint32_t>(Disp) & 0x3fff));
      return;
    }
    }
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    if (TT.isOSBinFormatCOFF()) {
      unsigned Machine = IsTASO ? COFF::IMAGE_FILE_MACHINE_ALPHA
                                : COFF::IMAGE_FILE_MACHINE_ALPHA64;
      return createAlphaWinCOFFObjectWriter(Machine);
    }
    if (TT.isOSBinFormatELF())
      return createAlphaELFObjectWriter(TT);

    llvm_unreachable("Alpha MC object format is not supported");
  }

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override {
    if (Count % 4)
      return false;

    for (uint64_t I = 0, E = Count / 4; I != E; ++I)
      support::endian::write<uint32_t>(OS, 0x2ffe0000, Endian);
    return true;
  }
};

} // end anonymous namespace

MCAsmBackend *llvm::createAlphaAsmBackend(const Target &T,
                                          const MCSubtargetInfo &STI,
                                          const MCRegisterInfo &MRI,
                                          const MCTargetOptions &Options) {
  SmallVector<StringRef, 8> Features;
  STI.getFeatureString().split(Features, ',');
  return new AlphaAsmBackend(STI.getTargetTriple(),
                             llvm::is_contained(Features, "+taso"));
}
