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

MCAsmBackend *createAlphaAsmBackend(const Target &T,
                                    const MCSubtargetInfo &STI,
                                    const MCRegisterInfo &MRI,
                                    const MCTargetOptions &Options);

MCCodeEmitter *createAlphaMCCodeEmitter(const MCInstrInfo &MCII,
                                        MCContext &Ctx);

std::unique_ptr<MCObjectTargetWriter>
createAlphaWinCOFFObjectWriter(unsigned Machine);

MCStreamer *createAlphaWinCOFFStreamer(MCContext &C,
                                       std::unique_ptr<MCAsmBackend> &&AB,
                                       std::unique_ptr<MCObjectWriter> &&OW,
                                       std::unique_ptr<MCCodeEmitter> &&CE);

void initLLVMToCVRegMapping(MCRegisterInfo *MRI);

void registerAlphaAsmParser();

namespace Alpha {
enum Fixups {
  fixup_Alpha_Branch = FirstTargetFixupKind,
  fixup_Alpha_REFHI,
  fixup_Alpha_REFLO,
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
