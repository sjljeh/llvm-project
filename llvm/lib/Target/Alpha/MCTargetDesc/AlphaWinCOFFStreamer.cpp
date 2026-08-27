//===-- AlphaWinCOFFStreamer.cpp - Alpha WinCOFF Streamer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AlphaMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {
class AlphaWinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::RISCUnwindEmitter EHStreamer;

public:
  AlphaWinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                       std::unique_ptr<MCCodeEmitter> CE,
                       std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)) {}

  void emitWinEHHandlerData(SMLoc Loc) override {
    MCStreamer::emitWinEHHandlerData(Loc);
    if (WinEH::FrameInfo *Frame = getCurrentWinFrameInfo()) {
      Frame = Frame->ChainedParent ? Frame->ChainedParent : Frame;
      EHStreamer.EmitUnwindInfo(*this, Frame, /*HandlerData=*/true);
    }
  }

  void emitWindowsUnwindTables(WinEH::FrameInfo *) override {}

  void emitWindowsUnwindTables() override {
    if (getNumWinFrameInfos())
      EHStreamer.Emit(*this);
  }

  void finishImpl() override {
    emitFrames();
    emitWindowsUnwindTables();
    MCWinCOFFStreamer::finishImpl();
  }
};
} // namespace

MCStreamer *llvm::createAlphaWinCOFFStreamer(
    MCContext &C, std::unique_ptr<MCAsmBackend> &&AB,
    std::unique_ptr<MCObjectWriter> &&OW,
    std::unique_ptr<MCCodeEmitter> &&CE) {
  return new AlphaWinCOFFStreamer(C, std::move(AB), std::move(CE),
                                  std::move(OW));
}
