//===-- RISCVWinCOFFStreamer.h - RISC-V64 RVUW COFF streamer ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVWINCOFFSTREAMER_H
#define LLVM_LIB_TARGET_RISCV_MCTARGETDESC_RISCVWINCOFFSTREAMER_H

#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCObjectWriter;
class MCStreamer;

MCStreamer *createRISCVWinCOFFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> &&MAB,
    std::unique_ptr<MCObjectWriter> &&OW,
    std::unique_ptr<MCCodeEmitter> &&Emitter);

} // namespace llvm

#endif
