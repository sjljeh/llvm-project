//===-- RISCVWinCOFFStreamer.cpp - RISC-V64 RVUW COFF streamer -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Encodes ReactOS RISC-V64 Windows unwind records (RVUW). See
// llvm/Support/RISCVWinEH.h.
//
//===----------------------------------------------------------------------===//

#include "RISCVWinCOFFStreamer.h"
#include "RISCVTargetStreamer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCWinCOFFStreamer.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/RISCVWinEH.h"
#include <cstdint>
#include <limits>
#include <optional>

using namespace llvm;

namespace {

struct EncodedCode {
  const MCSymbol *Label;
  const MCSymbol *Base;
  std::optional<uint32_t> CodeOffset;
  uint32_t OpInfo;
  /// The extension word of a two-slot opcode.
  std::optional<int32_t> ExtensionOperand;
  unsigned Destination;
};

struct EpilogEmitInfo {
  const MCSymbol *Start;
  const MCSymbol *End;
  uint16_t FirstCodeSlot;
  uint16_t CodeSlots;
  SmallVector<EncodedCode, 8> Codes;
};

/// Per-stream RVUW data that WinEH::Instruction and WinEH::FrameInfo have no
/// field for.
struct RVUWSideTables {
  /// The VLENB multiplier of each scaled rule, keyed by its label.
  DenseMap<const MCSymbol *, int32_t> Scales;
  /// An explicit register-relative establisher frame per function.
  DenseMap<const WinEH::FrameInfo *, std::pair<uint8_t, int32_t>> Establishers;
};

static std::optional<int64_t> getAbsDifference(MCStreamer &Streamer,
                                               const MCSymbol *LHS,
                                               const MCSymbol *RHS) {
  auto &Assembler = static_cast<MCObjectStreamer &>(Streamer).getAssembler();
  MCContext &Context = Streamer.getContext();
  const MCExpr *Difference =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(LHS, Context),
                              MCSymbolRefExpr::create(RHS, Context), Context);
  int64_t Value;
  if (!Difference->evaluateAsAbsolute(Value, Assembler))
    return std::nullopt;
  return Value;
}

static bool reportFrameError(MCStreamer &Streamer, const WinEH::FrameInfo &Info,
                             const Twine &Message) {
  Streamer.getContext().reportError(
      Info.FunctionLoc,
      "invalid RISC-V64 RVUW for " + Info.Function->getName() + ": " + Message);
  return false;
}

static bool validateCodeOffset(MCStreamer &Streamer,
                               const WinEH::FrameInfo &Info,
                               const WinEH::Instruction &Instruction,
                               const MCSymbol *Base, uint32_t Limit,
                               std::optional<uint32_t> &CodeOffset) {
  if (!Instruction.Label)
    return reportFrameError(Streamer, Info,
                            "state transition has no instruction label");
  std::optional<int64_t> Difference =
      getAbsDifference(Streamer, Instruction.Label, Base);
  if (!Difference) {
    CodeOffset.reset();
    return true;
  }
  if (*Difference < 0 || *Difference > Limit || *Difference > UINT32_MAX)
    return reportFrameError(Streamer, Info,
                            "state-transition offset is outside its range");
  if (*Difference & 1)
    return reportFrameError(Streamer, Info,
                            "state-transition offset is not two-byte aligned");
  CodeOffset = static_cast<uint32_t>(*Difference);
  return true;
}

static bool encodeCode(MCStreamer &Streamer, const WinEH::FrameInfo &Info,
                       const RVUWSideTables &Tables,
                       const WinEH::Instruction &Instruction,
                       const MCSymbol *Base, uint32_t Limit, EncodedCode &Code,
                       uint16_t &RequiredState) {
  if (!validateCodeOffset(Streamer, Info, Instruction, Base, Limit,
                          Code.CodeOffset))
    return false;
  Code.Label = Instruction.Label;
  Code.Base = Base;

  const unsigned Opcode = Instruction.Operation;
  const unsigned Register = Instruction.Register;
  const int32_t Operand = static_cast<int32_t>(Instruction.Offset);
  int32_t Scale = 0;
  if (RISCVWinEH::isScaledOpcode(Opcode)) {
    auto It = Tables.Scales.find(Instruction.Label);
    if (It != Tables.Scales.end())
      Scale = It->second;
  }
  if (RISCVWinEH::isLargeOpcode(Opcode))
    return reportFrameError(Streamer, Info,
                            "large opcodes are selected by the encoder");
  if (StringRef Message =
          RISCVWinEH::validateRule(Opcode, Register, Operand, Scale);
      !Message.empty())
    return reportFrameError(Streamer, Info, Message);

  if (RISCVWinEH::isScaledOpcode(Opcode)) {
    // The VLENB multiplier is inline; the fixed offset is the extension word.
    uint32_t EncodedScale =
        static_cast<uint32_t>(Scale) & RISCVWinEH::OperandMask;
    Code.OpInfo = Opcode | (Register << RISCVWinEH::RegisterShift) |
                  (EncodedScale << RISCVWinEH::OperandShift);
    Code.ExtensionOperand = Operand;
  } else if (Operand < RISCVWinEH::SmallOperandMin ||
             Operand > RISCVWinEH::SmallOperandMax) {
    unsigned Large = RISCVWinEH::getLargeOpcode(Opcode);
    if (!Large)
      return reportFrameError(Streamer, Info,
                              "operand is outside the opcode range");
    Code.OpInfo = Large | (Register << RISCVWinEH::RegisterShift);
    Code.ExtensionOperand = Operand;
  } else {
    uint32_t EncodedOperand =
        static_cast<uint32_t>(Operand) & RISCVWinEH::OperandMask;
    Code.OpInfo = Opcode | (Register << RISCVWinEH::RegisterShift) |
                  (EncodedOperand << RISCVWinEH::OperandShift);
  }
  Code.Destination = RISCVWinEH::getCodeDestination(Opcode, Register);
  RequiredState |= RISCVWinEH::getRequiredState(Opcode);
  return true;
}

static bool encodeProgram(MCStreamer &Streamer, const WinEH::FrameInfo &Info,
                          const RVUWSideTables &Tables,
                          ArrayRef<WinEH::Instruction> Instructions,
                          const MCSymbol *Base, uint32_t Limit,
                          SmallVectorImpl<EncodedCode> &Codes,
                          uint16_t &CodeSlots, uint16_t &RequiredState) {
  std::optional<uint32_t> PreviousOffset;
  uint32_t Slots = 0;
  for (const WinEH::Instruction &Instruction : Instructions) {
    EncodedCode Code;
    if (!encodeCode(Streamer, Info, Tables, Instruction, Base, Limit, Code,
                    RequiredState))
      return false;
    if (PreviousOffset && Code.CodeOffset &&
        *Code.CodeOffset < *PreviousOffset)
      return reportFrameError(Streamer, Info,
                              "state-transition offsets are not sorted");
    for (const EncodedCode &Previous : Codes)
      if (Previous.CodeOffset && Code.CodeOffset &&
          *Previous.CodeOffset == *Code.CodeOffset &&
          Previous.Destination == Code.Destination)
        return reportFrameError(
            Streamer, Info,
            "two state transitions conflict at the same code offset");
    PreviousOffset = Code.CodeOffset;
    Slots += Code.ExtensionOperand ? 2 : 1;
    if (Slots > UINT16_MAX)
      return reportFrameError(Streamer, Info,
                              "state program exceeds 65535 slots");
    Codes.push_back(Code);
  }
  CodeSlots = static_cast<uint16_t>(Slots);
  return true;
}

/// Without an explicit establisher, the establisher frame is the body CFA
/// minus the fixed part of the last prologue CFA rule: the frame pointer, or
/// the stack pointer after the fixed allocation.
static int32_t getEstablisherFrameOffset(ArrayRef<WinEH::Instruction> Source) {
  int32_t Offset = 0;
  for (const WinEH::Instruction &Instruction : Source)
    if (Instruction.Operation == RISCVWinEH::UOP_SetCFA ||
        Instruction.Operation == RISCVWinEH::UOP_SetCFAScaled)
      Offset = -static_cast<int32_t>(Instruction.Offset);
  return Offset;
}

static void emitDifference32(MCStreamer &Streamer, const MCSymbol *LHS,
                             const MCSymbol *RHS) {
  MCContext &Context = Streamer.getContext();
  Streamer.emitValue(
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(LHS, Context),
                              MCSymbolRefExpr::create(RHS, Context), Context),
      4);
}

static void emitCode(MCStreamer &Streamer, const EncodedCode &Code) {
  if (Code.CodeOffset)
    Streamer.emitInt32(*Code.CodeOffset);
  else
    emitDifference32(Streamer, Code.Label, Code.Base);
  Streamer.emitInt32(Code.OpInfo);
  if (Code.ExtensionOperand) {
    Streamer.emitInt32(static_cast<uint32_t>(*Code.ExtensionOperand));
    Streamer.emitInt32(0);
  }
}

static void emitImageRelative32(MCStreamer &Streamer, const MCSymbol *Symbol) {
  Streamer.emitValue(MCSymbolRefExpr::create(Symbol,
                                             MCSymbolRefExpr::VK_COFF_IMGREL32,
                                             Streamer.getContext()),
                     4);
}

class RISCV64UnwindEmitter {
  const RVUWSideTables &Tables;

public:
  explicit RISCV64UnwindEmitter(const RVUWSideTables &Tables)
      : Tables(Tables) {}
  void emit(MCStreamer &Streamer) const;
  void emitUnwindInfo(MCStreamer &Streamer, WinEH::FrameInfo *Info,
                      bool HandlerData) const;

private:
  void emitRuntimeFunction(MCStreamer &Streamer,
                           const WinEH::FrameInfo &Info) const;
};

void RISCV64UnwindEmitter::emitUnwindInfo(MCStreamer &Streamer,
                                          WinEH::FrameInfo *Info,
                                          bool HandlerData) const {
  if (Info->Symbol || Info->EmitAttempted)
    return;
  Info->EmitAttempted = true;

  if (Info->Version != WinEH::FrameInfo::DefaultVersion) {
    reportFrameError(Streamer, *Info,
                     "'.seh_unwindversion' does not apply to RVUW");
    return;
  }
  if (Info->ChainedParent &&
      (Info->HandlesExceptions || Info->HandlesUnwind ||
       Info->ExceptionHandler)) {
    reportFrameError(Streamer, *Info,
                     "a chained record cannot carry an independent handler");
    return;
  }
  if (Info->ChainedParent && !Info->Instructions.empty()) {
    reportFrameError(Streamer, *Info,
                     "a chained record cannot carry a prologue program");
    return;
  }
  if ((Info->HandlesExceptions || Info->HandlesUnwind) &&
      !Info->ExceptionHandler) {
    reportFrameError(Streamer, *Info, "handler flags have no handler symbol");
    return;
  }

  const MCSymbol *PrologEnd = Info->PrologEnd ? Info->PrologEnd : Info->Begin;
  std::optional<int64_t> PrologSize =
      getAbsDifference(Streamer, PrologEnd, Info->Begin);
  if (PrologSize &&
      (*PrologSize < 0 || *PrologSize > UINT32_MAX || (*PrologSize & 1))) {
    reportFrameError(Streamer, *Info,
                     "prologue end is unaligned or out of range");
    return;
  }
  if (!Info->PrologEnd && !Info->Instructions.empty()) {
    reportFrameError(Streamer, *Info, "a nonempty prologue has no end marker");
    return;
  }

  uint16_t RequiredState = RISCVWinEH::STATE_Integer;
  SmallVector<EncodedCode, 16> PrologCodes;
  uint16_t PrologCodeSlots = 0;
  if (!encodeProgram(Streamer, *Info, Tables, Info->Instructions, Info->Begin,
                     PrologSize ? static_cast<uint32_t>(*PrologSize)
                                : UINT32_MAX,
                     PrologCodes, PrologCodeSlots, RequiredState))
    return;

  SmallVector<EpilogEmitInfo, 4> Epilogs;
  uint32_t TotalEpilogSlots = 0;
  std::optional<uint32_t> PreviousEnd;
  for (const auto &[Symbol, Epilog] : Info->EpilogMap) {
    if (!Epilog.Start || !Epilog.End) {
      reportFrameError(Streamer, *Info, "epilogue scope is incomplete");
      return;
    }
    std::optional<int64_t> Start =
        getAbsDifference(Streamer, Epilog.Start, Info->Begin);
    std::optional<int64_t> End =
        getAbsDifference(Streamer, Epilog.End, Info->Begin);
    if ((Start && (*Start < 0 || *Start > UINT32_MAX)) ||
        (End && (*End < 0 || *End > UINT32_MAX)) ||
        (Start && End && *End <= *Start)) {
      reportFrameError(Streamer, *Info,
                       "epilogue range is empty or outside the function");
      return;
    }
    if ((Start && (*Start & 1)) || (End && (*End & 1))) {
      reportFrameError(Streamer, *Info,
                       "epilogue range is not two-byte aligned");
      return;
    }
    if (PreviousEnd && Start && static_cast<uint32_t>(*Start) < *PreviousEnd) {
      reportFrameError(Streamer, *Info, "epilogue scopes overlap");
      return;
    }

    EpilogEmitInfo EmitInfo;
    EmitInfo.Start = Epilog.Start;
    EmitInfo.End = Epilog.End;
    EmitInfo.FirstCodeSlot = static_cast<uint16_t>(TotalEpilogSlots);
    uint32_t Length =
        Start && End ? static_cast<uint32_t>(*End - *Start) : UINT32_MAX;
    if (!encodeProgram(Streamer, *Info, Tables, Epilog.Instructions,
                       Epilog.Start, Length, EmitInfo.Codes, EmitInfo.CodeSlots,
                       RequiredState))
      return;
    TotalEpilogSlots += EmitInfo.CodeSlots;
    if (TotalEpilogSlots > UINT16_MAX) {
      reportFrameError(Streamer, *Info, "epilogue programs exceed 65535 slots");
      return;
    }
    if (End)
      PreviousEnd = static_cast<uint32_t>(*End);
    else
      PreviousEnd.reset();
    Epilogs.push_back(std::move(EmitInfo));
  }
  if (Epilogs.size() > UINT16_MAX) {
    reportFrameError(Streamer, *Info, "too many epilogue scopes");
    return;
  }

  uint8_t EstablisherRegister = 0;
  int32_t EstablisherFrameOffset =
      getEstablisherFrameOffset(Info->Instructions);
  if (auto It = Tables.Establishers.find(Info);
      It != Tables.Establishers.end()) {
    if (Info->ChainedParent) {
      reportFrameError(Streamer, *Info,
                       "a chained record cannot name an establisher");
      return;
    }
    EstablisherRegister = It->second.first;
    EstablisherFrameOffset = It->second.second;
  }

  uint32_t Flags = 0;
  if (Info->HandlesExceptions)
    Flags |= RISCVWinEH::UNW_ExceptionHandler;
  if (Info->HandlesUnwind)
    Flags |= RISCVWinEH::UNW_TerminateHandler;
  if (Info->ChainedParent)
    Flags |= RISCVWinEH::UNW_ChainInfo;

  uint64_t RecordSize = RISCVWinEH::UnwindHeaderSize;
  RecordSize += uint64_t(PrologCodeSlots) * RISCVWinEH::UnwindCodeSize;
  RecordSize += uint64_t(Epilogs.size()) * RISCVWinEH::EpilogScopeSize;
  RecordSize += uint64_t(TotalEpilogSlots) * RISCVWinEH::UnwindCodeSize;
  if (Info->ChainedParent)
    RecordSize += RISCVWinEH::RuntimeFunctionSize;
  else if (Info->ExceptionHandler)
    RecordSize += RISCVWinEH::HandlerSize;
  RecordSize = alignTo(RecordSize, 4);
  if (RecordSize > UINT32_MAX) {
    reportFrameError(Streamer, *Info, "metadata record exceeds four GiB");
    return;
  }

  MCSection *XData = Streamer.getAssociatedXDataSection(Info->TextSection);
  Streamer.switchSection(XData);
  Streamer.emitValueToAlignment(Align(4));
  MCSymbol *Record = Streamer.getContext().createTempSymbol("rvuw");
  Streamer.emitLabel(Record);
  Info->Symbol = Record;

  Streamer.emitInt32(RISCVWinEH::UnwindMagic);
  Streamer.emitInt16(RISCVWinEH::UnwindHeaderSize);
  Streamer.emitInt8(EstablisherRegister);
  Streamer.emitInt8(0);
  Streamer.emitInt32(static_cast<uint32_t>(RecordSize));
  Streamer.emitInt32(Flags);
  if (PrologSize)
    Streamer.emitInt32(static_cast<uint32_t>(*PrologSize));
  else
    emitDifference32(Streamer, PrologEnd, Info->Begin);
  Streamer.emitInt32(static_cast<uint32_t>(EstablisherFrameOffset));
  Streamer.emitInt16(PrologCodeSlots);
  Streamer.emitInt16(static_cast<uint16_t>(Epilogs.size()));
  Streamer.emitInt16(static_cast<uint16_t>(TotalEpilogSlots));
  Streamer.emitInt16(RequiredState);

  for (const EncodedCode &Code : PrologCodes)
    emitCode(Streamer, Code);
  for (const EpilogEmitInfo &Epilog : Epilogs) {
    emitDifference32(Streamer, Epilog.Start, Info->Begin);
    emitDifference32(Streamer, Epilog.End, Info->Begin);
    Streamer.emitInt16(Epilog.FirstCodeSlot);
    Streamer.emitInt16(Epilog.CodeSlots);
    Streamer.emitInt32(0);
  }
  for (const EpilogEmitInfo &Epilog : Epilogs)
    for (const EncodedCode &Code : Epilog.Codes)
      emitCode(Streamer, Code);

  if (Info->ChainedParent) {
    emitImageRelative32(Streamer, Info->ChainedParent->Begin);
    emitImageRelative32(Streamer, Info->ChainedParent->End);
    emitImageRelative32(Streamer, Info->ChainedParent->Symbol);
  } else if (Info->ExceptionHandler) {
    emitImageRelative32(Streamer, Info->ExceptionHandler);
    if (HandlerData) {
      MCSymbol *HandlerDataSymbol =
          Streamer.getContext().createTempSymbol("rvuw_handler_data");
      emitImageRelative32(Streamer, HandlerDataSymbol);
      Streamer.emitValueToAlignment(Align(4));
      Streamer.emitLabel(HandlerDataSymbol);
    } else {
      Streamer.emitInt32(0);
    }
  }
  Streamer.emitValueToAlignment(Align(4));
}

void RISCV64UnwindEmitter::emitRuntimeFunction(
    MCStreamer &Streamer, const WinEH::FrameInfo &Info) const {
  if (!Info.Symbol || !Info.End)
    return;
  std::optional<int64_t> Length =
      getAbsDifference(Streamer, Info.End, Info.Begin);
  if (Length && (*Length <= 0 || *Length > UINT32_MAX || (*Length & 1))) {
    reportFrameError(Streamer, Info,
                     "function range is unresolved, empty, or unaligned");
    return;
  }
  MCSection *PData = Streamer.getAssociatedPDataSection(Info.TextSection);
  Streamer.switchSection(PData);
  Streamer.emitValueToAlignment(Align(4));
  emitImageRelative32(Streamer, Info.Begin);
  emitImageRelative32(Streamer, Info.End);
  emitImageRelative32(Streamer, Info.Symbol);
}

void RISCV64UnwindEmitter::emit(MCStreamer &Streamer) const {
  for (const auto &Frame : Streamer.getWinFrameInfos())
    emitUnwindInfo(Streamer, Frame.get(), false);
  for (const auto &Frame : Streamer.getWinFrameInfos())
    emitRuntimeFunction(Streamer, *Frame);
}

class RISCVWinCOFFStreamer final : public MCWinCOFFStreamer {
  RVUWSideTables Tables;
  RISCV64UnwindEmitter UnwindEmitter{Tables};

public:
  RISCVWinCOFFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
                       std::unique_ptr<MCCodeEmitter> Emitter,
                       std::unique_ptr<MCObjectWriter> Writer)
      : MCWinCOFFStreamer(Context, std::move(MAB), std::move(Emitter),
                          std::move(Writer)) {}

  RVUWSideTables &getRVUWSideTables() { return Tables; }

  void emitWinEHHandlerData(SMLoc Loc) override {
    MCStreamer::emitWinEHHandlerData(Loc);
    if (WinEH::FrameInfo *Frame = getCurrentWinFrameInfo())
      UnwindEmitter.emitUnwindInfo(*this, Frame, true);
  }

  void emitWindowsUnwindTables(WinEH::FrameInfo *Frame) override {
    UnwindEmitter.emitUnwindInfo(*this, Frame, false);
  }

  void emitWindowsUnwindTables() override { UnwindEmitter.emit(*this); }

  void finishImpl() override {
    emitFrames();
    emitWindowsUnwindTables();
    MCWinCOFFStreamer::finishImpl();
  }
};

} // end anonymous namespace

void RISCVTargetWinCOFFStreamer::emitRVUWRule(unsigned Opcode,
                                              unsigned Register,
                                              int64_t Operand, int64_t Scale) {
  MCStreamer &S = getStreamer();
  WinEH::FrameInfo *Frame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!Frame)
    return;
  if (StringRef Message =
          RISCVWinEH::validateRule(Opcode, Register, Operand, Scale);
      !Message.empty()) {
    S.getContext().reportError(SMLoc(),
                               Twine("invalid RISC-V64 RVUW rule: ") + Message);
    return;
  }

  MCSymbol *Label = S.emitCFILabel();
  if (RISCVWinEH::isScaledOpcode(Opcode))
    static_cast<RISCVWinCOFFStreamer &>(S).getRVUWSideTables().Scales[Label] =
        static_cast<int32_t>(Scale);
  WinEH::Instruction Inst(Opcode, Label, Register,
                          static_cast<uint32_t>(Operand));
  if (S.isInEpilogCFI())
    S.getCurrentWinEpilog()->Instructions.push_back(Inst);
  else
    Frame->Instructions.push_back(Inst);
}

void RISCVTargetWinCOFFStreamer::emitRVUWEstablisher(unsigned Register,
                                                     int64_t Offset) {
  MCStreamer &S = getStreamer();
  WinEH::FrameInfo *Frame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!Frame)
    return;
  if (!RISCVWinEH::isEstablisherRegister(Register) || !isInt<32>(Offset)) {
    S.getContext().reportError(
        SMLoc(), "RVUW establisher must be sp or a nonvolatile register "
                 "with a 32-bit offset");
    return;
  }
  static_cast<RISCVWinCOFFStreamer &>(S)
      .getRVUWSideTables()
      .Establishers[Frame] = {static_cast<uint8_t>(Register),
                              static_cast<int32_t>(Offset)};
}

void RISCVTargetWinCOFFStreamer::emitRVUWPrologEnd() {
  getStreamer().emitWinCFIEndProlog();
}

void RISCVTargetWinCOFFStreamer::emitRVUWEpilogStart() {
  getStreamer().emitWinCFIBeginEpilogue();
}

void RISCVTargetWinCOFFStreamer::emitRVUWEpilogEnd() {
  getStreamer().emitWinCFIEndEpilogue();
}

MCStreamer *llvm::createRISCVWinCOFFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> &&MAB,
    std::unique_ptr<MCObjectWriter> &&OW,
    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return new RISCVWinCOFFStreamer(Context, std::move(MAB), std::move(Emitter),
                                  std::move(OW));
}
