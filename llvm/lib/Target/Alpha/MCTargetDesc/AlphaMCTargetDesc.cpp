//===-- AlphaMCTargetDesc.cpp - Alpha Target Descriptions -------*- C++ -*-===//
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

#include "AlphaMCTargetDesc.h"
#include "AlphaInstPrinter.h"
#include "AlphaMCAsmInfo.h"
#include "TargetInfo/AlphaTargetInfo.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

#define GET_INSTRINFO_MC_DESC
#include "AlphaGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "AlphaGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "AlphaGenRegisterInfo.inc"

using namespace llvm;


static MCInstrInfo *createAlphaMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitAlphaMCInstrInfo(X);
  return X;
}

static MCInstPrinter *createAlphaMCInstPrinter(const Triple &TT,
                                               unsigned SyntaxVariant,
                                               const MCAsmInfo &MAI,
                                               const MCInstrInfo &MII,
                                               const MCRegisterInfo &MRI) {
  return new AlphaInstPrinter(MAI, MII, MRI);
}

void llvm::initLLVMToCVRegMapping(MCRegisterInfo *MRI) {
  struct RegMapEntry {
    codeview::RegisterId CVReg;
    MCRegister Reg;
  };
  const RegMapEntry RegMap[] = {
#define MAP_FPR(N) {codeview::RegisterId::ALPHA_F##N, Alpha::F##N}
      MAP_FPR(0),  MAP_FPR(1),  MAP_FPR(2),  MAP_FPR(3),
      MAP_FPR(4),  MAP_FPR(5),  MAP_FPR(6),  MAP_FPR(7),
      MAP_FPR(8),  MAP_FPR(9),  MAP_FPR(10), MAP_FPR(11),
      MAP_FPR(12), MAP_FPR(13), MAP_FPR(14), MAP_FPR(15),
      MAP_FPR(16), MAP_FPR(17), MAP_FPR(18), MAP_FPR(19),
      MAP_FPR(20), MAP_FPR(21), MAP_FPR(22), MAP_FPR(23),
      MAP_FPR(24), MAP_FPR(25), MAP_FPR(26), MAP_FPR(27),
      MAP_FPR(28), MAP_FPR(29), MAP_FPR(30), MAP_FPR(31),
#undef MAP_FPR
      {codeview::RegisterId::ALPHA_V0, Alpha::R0},
      {codeview::RegisterId::ALPHA_T0, Alpha::R1},
      {codeview::RegisterId::ALPHA_T1, Alpha::R2},
      {codeview::RegisterId::ALPHA_T2, Alpha::R3},
      {codeview::RegisterId::ALPHA_T3, Alpha::R4},
      {codeview::RegisterId::ALPHA_T4, Alpha::R5},
      {codeview::RegisterId::ALPHA_T5, Alpha::R6},
      {codeview::RegisterId::ALPHA_T6, Alpha::R7},
      {codeview::RegisterId::ALPHA_T7, Alpha::R8},
      {codeview::RegisterId::ALPHA_S0, Alpha::R9},
      {codeview::RegisterId::ALPHA_S1, Alpha::R10},
      {codeview::RegisterId::ALPHA_S2, Alpha::R11},
      {codeview::RegisterId::ALPHA_S3, Alpha::R12},
      {codeview::RegisterId::ALPHA_S4, Alpha::R13},
      {codeview::RegisterId::ALPHA_S5, Alpha::R14},
      {codeview::RegisterId::ALPHA_FP, Alpha::R15},
      {codeview::RegisterId::ALPHA_A0, Alpha::R16},
      {codeview::RegisterId::ALPHA_A1, Alpha::R17},
      {codeview::RegisterId::ALPHA_A2, Alpha::R18},
      {codeview::RegisterId::ALPHA_A3, Alpha::R19},
      {codeview::RegisterId::ALPHA_A4, Alpha::R20},
      {codeview::RegisterId::ALPHA_A5, Alpha::R21},
      {codeview::RegisterId::ALPHA_T8, Alpha::R22},
      {codeview::RegisterId::ALPHA_T9, Alpha::R23},
      {codeview::RegisterId::ALPHA_T10, Alpha::R24},
      {codeview::RegisterId::ALPHA_T11, Alpha::R25},
      {codeview::RegisterId::ALPHA_RA, Alpha::R26},
      {codeview::RegisterId::ALPHA_T12, Alpha::R27},
      {codeview::RegisterId::ALPHA_AT, Alpha::R28},
      {codeview::RegisterId::ALPHA_GP, Alpha::R29},
      {codeview::RegisterId::ALPHA_SP, Alpha::R30},
      {codeview::RegisterId::ALPHA_ZERO, Alpha::R31},
  };
  for (const auto &I : RegMap)
    MRI->mapLLVMRegToCVReg(I.Reg, static_cast<int>(I.CVReg));
}

static MCRegisterInfo *createAlphaMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitAlphaMCRegisterInfo(X, Alpha::R26);
  initLLVMToCVRegMapping(X);
  return X;
}

static MCSubtargetInfo *createAlphaMCSubtargetInfo(const Triple &TT,
                                                   StringRef CPU,
                                                   StringRef FS) {
  return createAlphaMCSubtargetInfoImpl(TT, CPU, /*TuneCPU=*/CPU, FS);
}

static MCAsmInfo *createAlphaMCAsmInfo(const MCRegisterInfo &MRI,
                                       const Triple &TT,
                                       const MCTargetOptions &Options) {
  if (TT.isOSBinFormatCOFF())
    return new AlphaMCAsmInfoMicrosoftCOFF(TT, Options);
  return new AlphaMCAsmInfo(TT, Options);
}

// Force static initialization.
extern "C" void LLVMInitializeAlphaTargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfoFn X(getTheAlphaTarget(), createAlphaMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(getTheAlphaTarget(), createAlphaMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(getTheAlphaTarget(), createAlphaMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(getTheAlphaTarget(),
                                           createAlphaMCSubtargetInfo);

  // Register the GNU-style MC instruction printer.
  TargetRegistry::RegisterMCInstPrinter(getTheAlphaTarget(),
                                        createAlphaMCInstPrinter);

  // Register the asm backend.
  TargetRegistry::RegisterMCAsmBackend(getTheAlphaTarget(),
                                       createAlphaAsmBackend);

  // Register the MC code emitter.
  TargetRegistry::RegisterMCCodeEmitter(getTheAlphaTarget(),
                                        createAlphaMCCodeEmitter);

  // Register the COFF streamer.
  TargetRegistry::RegisterCOFFStreamer(getTheAlphaTarget(),
                                       createAlphaWinCOFFStreamer);

  registerAlphaAsmParser();
}
