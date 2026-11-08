//===- PPCWinCOFFStreamer.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "PPCMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {
class PPCWinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::RISCUnwindEmitter EHStreamer;
  Win64EH::NTUWUnwindEmitter NTUWStreamer;
  bool UsesNTUW;

public:
  PPCWinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                     std::unique_ptr<MCCodeEmitter> CE,
                     std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)),
        UsesNTUW(C.getAsmInfo().getWinEHEncodingType() ==
                 WinEH::EncodingType::NTUW) {}

  void emitWinEHHandlerData(SMLoc Loc) override;
  void emitWindowsUnwindTables(WinEH::FrameInfo *Frame) override;
  void emitWindowsUnwindTables() override;
  void finishImpl() override;
};

void PPCWinCOFFStreamer::emitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::emitWinEHHandlerData(Loc);

  if (WinEH::FrameInfo *CurFrame = getCurrentWinFrameInfo()) {
    if (UsesNTUW)
      return NTUWStreamer.EmitUnwindInfo(*this, CurFrame,
                                         /*HandlerData=*/true);
    CurFrame = CurFrame->ChainedParent ? CurFrame->ChainedParent : CurFrame;
    EHStreamer.EmitUnwindInfo(*this, CurFrame, /*HandlerData=*/true);
  }
}

void PPCWinCOFFStreamer::emitWindowsUnwindTables(WinEH::FrameInfo *) {
  // Emit all legacy RISC pdata after any late handler data.
}

void PPCWinCOFFStreamer::emitWindowsUnwindTables() {
  if (!getNumWinFrameInfos())
    return;
  if (UsesNTUW)
    NTUWStreamer.Emit(*this);
  else
    EHStreamer.Emit(*this);
}

void PPCWinCOFFStreamer::finishImpl() {
  emitFrames();
  if (UsesNTUW)
    Win64EH::NTUWUnwindEmitter::emitObjectMarker(*this);
  emitWindowsUnwindTables();
  MCWinCOFFStreamer::finishImpl();
}
} // namespace

MCStreamer *llvm::createPPCWinCOFFStreamer(
    MCContext &C, std::unique_ptr<MCAsmBackend> &&AB,
    std::unique_ptr<MCObjectWriter> &&OW, std::unique_ptr<MCCodeEmitter> &&CE) {
  return new PPCWinCOFFStreamer(C, std::move(AB), std::move(CE),
                               std::move(OW));
}
