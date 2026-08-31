//===-- AlphaMCTargetDesc.h - Alpha Target Descriptions ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file provides Alpha specific target descriptions.
//
//===----------------------------------------------------------------------===//

#ifndef ALPHAMCTARGETDESC_H
#define ALPHAMCTARGETDESC_H

#include "llvm/MC/MCFixup.h"
#include <memory>

namespace llvm {
class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCInstrInfo;
class MCObjectWriter;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCStreamer;
class MCTargetOptions;
class StringRef;
class Target;
class Triple;

MCAsmBackend *createAlphaAsmBackend(const Target &T,
                                    const MCSubtargetInfo &STI,
                                    const MCRegisterInfo &MRI,
                                    const MCTargetOptions &Options);

MCCodeEmitter *createAlphaMCCodeEmitter(const MCInstrInfo &MCII,
                                        MCContext &Ctx);

std::unique_ptr<MCObjectTargetWriter>
createAlphaWinCOFFObjectWriter(unsigned Machine);
std::unique_ptr<MCObjectTargetWriter>
createAlphaELFObjectWriter(const Triple &TT);

MCStreamer *createAlphaWinCOFFStreamer(MCContext &C,
                                       std::unique_ptr<MCAsmBackend> &&AB,
                                       std::unique_ptr<MCObjectWriter> &&OW,
                                       std::unique_ptr<MCCodeEmitter> &&CE);

void initLLVMToCVRegMapping(MCRegisterInfo *MRI);

void registerAlphaAsmParser();

namespace Alpha {
enum Specifier : uint16_t {
  S_None = 0,
  S_LITERAL,
  S_LITUSE_ADDR,
  S_LITUSE_BASE,
  S_LITUSE_BYTOFF,
  S_LITUSE_JSR,
  S_LITUSE_TLSGD,
  S_LITUSE_TLSLDM,
  S_LITUSE_JSRDIRECT,
  S_GPDISP,
  S_GPRELHIGH,
  S_GPRELLOW,
  S_GPREL16,
  S_GPREL32,
  S_BRSGP,
  S_TLSGD,
  S_TLSLDM,
  S_GOTDTPREL,
  S_DTPRELHI,
  S_DTPRELLO,
  S_DTPREL16,
  S_GOTTPREL,
  S_TPRELHI,
  S_TPRELLO,
  S_TPREL16,
};

enum Fixups {
  fixup_Alpha_Branch = FirstTargetFixupKind,
  fixup_Alpha_REFHI,
  fixup_Alpha_REFLO,
  fixup_Alpha_LITERAL,
  fixup_Alpha_LITUSE,
  fixup_Alpha_GPDISP,
  fixup_Alpha_BRSGP,
  fixup_Alpha_HINT,
  fixup_Alpha_GPREL16,
  fixup_Alpha_TLSGD,
  fixup_Alpha_TLSLDM,
  fixup_Alpha_GOTDTPREL,
  fixup_Alpha_DTPRELHI,
  fixup_Alpha_DTPRELLO,
  fixup_Alpha_DTPREL16,
  fixup_Alpha_GOTTPREL,
  fixup_Alpha_TPRELHI,
  fixup_Alpha_TPRELLO,
  fixup_Alpha_TPREL16,
  NumTargetFixupKinds
};
}

} // End llvm namespace

// Defines symbolic names for Alpha registers.  This defines a mapping from
// register name to register number.
//
#define GET_REGINFO_ENUM
#include "AlphaGenRegisterInfo.inc"

// Defines symbolic names for the Alpha instructions.
//
#define GET_INSTRINFO_ENUM
#include "AlphaGenInstrInfo.inc"

#define GET_SUBTARGETINFO_ENUM
#include "AlphaGenSubtargetInfo.inc"

#endif
