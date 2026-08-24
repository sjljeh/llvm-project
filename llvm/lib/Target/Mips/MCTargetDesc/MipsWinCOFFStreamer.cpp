//===- MipsWinCOFFStreamer.cpp-----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "MipsMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {
class MipsWinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::RISCUnwindEmitter EHStreamer;

public:
  MipsWinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                      std::unique_ptr<MCCodeEmitter> CE,
                      std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)) {}

  void emitWinEHHandlerData(SMLoc Loc) override;
  void emitWindowsUnwindTables(WinEH::FrameInfo *Frame) override;
  void emitWindowsUnwindTables() override;
  void finishImpl() override;
};

void MipsWinCOFFStreamer::emitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::emitWinEHHandlerData(Loc);

  if (WinEH::FrameInfo *CurFrame = getCurrentWinFrameInfo()) {
    CurFrame = CurFrame->ChainedParent ? CurFrame->ChainedParent : CurFrame;
    EHStreamer.EmitUnwindInfo(*this, CurFrame, /*HandlerData=*/true);
  }
}

void MipsWinCOFFStreamer::emitWindowsUnwindTables(WinEH::FrameInfo *) {
  // Emit all RISC pdata at finish time, after any late handler data.
}

void MipsWinCOFFStreamer::emitWindowsUnwindTables() {
  if (getNumWinFrameInfos())
    EHStreamer.Emit(*this);
}

void MipsWinCOFFStreamer::finishImpl() {
  emitFrames();
  emitWindowsUnwindTables();
  MCWinCOFFStreamer::finishImpl();
}
} // namespace

MCStreamer *llvm::createMipsWinCOFFStreamer(
    MCContext &C, std::unique_ptr<MCAsmBackend> &&AB,
    std::unique_ptr<MCObjectWriter> &&OW, std::unique_ptr<MCCodeEmitter> &&CE) {
  return new MipsWinCOFFStreamer(C, std::move(AB), std::move(CE),
                                 std::move(OW));
}
