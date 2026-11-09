//===- MCNTRISCWinEH.cpp - Alpha/MIPS/PowerPC NTUW unwind emission --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Encodes NTUW .xdata records and 8-byte .pdata entries for Alpha, MIPS and
// PowerPC Windows code built for MSVC 13.0 or later. The format is specified
// in llvm/Support/NTRISCWinEH.h.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/NTRISCWinEH.h"
#include <optional>

using namespace llvm;
using namespace llvm::NTRISCWinEH;

namespace {

struct EncodedCode {
  const MCSymbol *Label;
  const MCSymbol *Base;
  std::optional<uint32_t> CodeOffset;
  uint32_t OpInfo;
  std::optional<int32_t> ExtensionOperand;
  unsigned Destination;
};

struct EncodedEpilog {
  const MCSymbol *Start;
  const MCSymbol *End;
  uint16_t FirstCodeSlot = 0;
  uint16_t CodeSlots = 0;
  SmallVector<EncodedCode, 8> Codes;
};

} // namespace

std::optional<Machine>
Win64EH::NTUWUnwindEmitter::getMachine(const Triple &TT) {
  switch (TT.getArch()) {
  case Triple::alpha:
    return Machine::Alpha;
  case Triple::mipsel:
    return Machine::MIPS;
  case Triple::ppcle:
    return Machine::PPC;
  default:
    return std::nullopt;
  }
}

static std::optional<Machine> getStreamerMachine(MCStreamer &Streamer) {
  return Win64EH::NTUWUnwindEmitter::getMachine(
      Streamer.getContext().getTargetTriple());
}

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
      "invalid NTUW record for " + Info.Function->getName() + ": " + Message);
  return false;
}

static void emitDifference32(MCStreamer &Streamer, const MCSymbol *LHS,
                             const MCSymbol *RHS) {
  MCContext &Context = Streamer.getContext();
  Streamer.emitValue(
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(LHS, Context),
                              MCSymbolRefExpr::create(RHS, Context), Context),
      4);
}

static void emitImageRelative32(MCStreamer &Streamer, const MCSymbol *Symbol) {
  Streamer.emitValue(MCSymbolRefExpr::create(Symbol,
                                             MCSymbolRefExpr::VK_COFF_IMGREL32,
                                             Streamer.getContext()),
                     4);
}

static bool encodeCode(MCStreamer &Streamer, Machine M,
                       const WinEH::FrameInfo &Info,
                       const WinEH::Instruction &Instruction,
                       const MCSymbol *Base, uint32_t Limit, EncodedCode &Code,
                       uint16_t &RequiredState) {
  if (!Instruction.Label)
    return reportFrameError(Streamer, Info,
                            "state transition has no instruction label");
  Code.Label = Instruction.Label;
  Code.Base = Base;
  if (std::optional<int64_t> Difference =
          getAbsDifference(Streamer, Instruction.Label, Base)) {
    if (*Difference < 0 || *Difference > Limit)
      return reportFrameError(Streamer, Info,
                              "state transition lies outside its range");
    if (*Difference & 3)
      return reportFrameError(
          Streamer, Info, "state transition is not at an instruction boundary");
    Code.CodeOffset = static_cast<uint32_t>(*Difference);
  }

  const unsigned Opcode = Instruction.Operation;
  const unsigned Register = Instruction.Register;
  const int32_t Operand = static_cast<int32_t>(Instruction.Offset);
  if (StringRef Message = validateRule(M, Opcode, Register, Operand);
      !Message.empty())
    return reportFrameError(Streamer, Info, Message);
  if (!isExtendedOpcode(Opcode) &&
      (Operand < SmallOperandMin || Operand > SmallOperandMax) &&
      !getLargeOpcode(Opcode))
    return reportFrameError(Streamer, Info,
                            "operand is outside the opcode's range");
  auto Slots = encodeRule(0, Opcode, Register, Operand);
  Code.OpInfo = Slots[0].second;
  if (Slots.size() == 2)
    Code.ExtensionOperand = static_cast<int32_t>(Slots[1].first);
  Code.Destination = getCodeDestination(Opcode, Register);
  RequiredState |= getRequiredState(Opcode);
  return true;
}

static bool encodeProgram(MCStreamer &Streamer, Machine M,
                          const WinEH::FrameInfo &Info,
                          ArrayRef<WinEH::Instruction> Instructions,
                          const MCSymbol *Base, uint32_t Limit,
                          SmallVectorImpl<EncodedCode> &Codes,
                          uint16_t &CodeSlots, uint16_t &RequiredState) {
  std::optional<uint32_t> PreviousOffset;
  uint32_t Slots = 0;
  for (const WinEH::Instruction &Instruction : Instructions) {
    EncodedCode Code;
    if (!encodeCode(Streamer, M, Info, Instruction, Base, Limit, Code,
                    RequiredState))
      return false;
    if (PreviousOffset && Code.CodeOffset && *Code.CodeOffset < *PreviousOffset)
      return reportFrameError(Streamer, Info,
                              "state transitions are not in address order");
    // A later rule for the same destination at the same offset supersedes an
    // earlier one; keep only the last.
    if (Code.CodeOffset)
      for (auto It = Codes.begin(); It != Codes.end(); ++It)
        if (It->CodeOffset && *It->CodeOffset == *Code.CodeOffset &&
            It->Destination == Code.Destination) {
          Slots -= It->ExtensionOperand ? 2 : 1;
          Codes.erase(It);
          break;
        }
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

void Win64EH::NTUWUnwindEmitter::EmitUnwindInfo(MCStreamer &Streamer,
                                                WinEH::FrameInfo *Info,
                                                bool HandlerData) const {
  // The record needs the function's length, which is not known while the
  // language-specific data is being written. Place that data now and the
  // record after it, once the function has ended.
  if (!HandlerData || HandlerDataSymbols.count(Info))
    return;
  MCSection *XData = Streamer.getAssociatedXDataSection(Info->TextSection);
  Streamer.switchSection(XData);
  Streamer.emitValueToAlignment(Align(4));
  MCSymbol *Label = Streamer.getContext().createTempSymbol("ntuw_handler_data");
  Streamer.emitLabel(Label);
  HandlerDataSymbols[Info] = Label;
}

void Win64EH::NTUWUnwindEmitter::emitRecord(MCStreamer &Streamer,
                                            WinEH::FrameInfo *Info) const {
  if (Info->Symbol || Info->EmitAttempted)
    return;
  Info->EmitAttempted = true;

  std::optional<Machine> M = getStreamerMachine(Streamer);
  if (!M) {
    reportFrameError(Streamer, *Info, "the target has no NTUW machine");
    return;
  }
  if (Info->Version != WinEH::FrameInfo::DefaultVersion) {
    reportFrameError(Streamer, *Info,
                     "'.seh_unwindversion' does not apply to NTUW");
    return;
  }
  if (Info->ChainedParent &&
      (Info->HandlesExceptions || Info->HandlesUnwind ||
       Info->ExceptionHandler || !Info->Instructions.empty() ||
       Info->Establisher)) {
    reportFrameError(Streamer, *Info,
                     "a chained record carries only epilogue programs");
    return;
  }
  if ((Info->HandlesExceptions || Info->HandlesUnwind) &&
      !Info->ExceptionHandler) {
    reportFrameError(Streamer, *Info, "handler flags have no handler symbol");
    return;
  }
  // Each record covers its own fragment: a parent split by
  // .seh_splitchained ends where its first chained record begins.
  const MCSymbol *FunctionEnd = Info->End ? Info->End : Info->FuncletOrFuncEnd;
  if (!FunctionEnd) {
    reportFrameError(Streamer, *Info, "function has no end");
    return;
  }
  if (std::optional<int64_t> Length =
          getAbsDifference(Streamer, FunctionEnd, Info->Begin);
      Length && (*Length <= 0 || (*Length & 3))) {
    reportFrameError(Streamer, *Info,
                     "function length is zero or not a whole number of "
                     "instructions");
    return;
  }
  if (!Info->PrologEnd && !Info->Instructions.empty()) {
    reportFrameError(Streamer, *Info, "a nonempty prologue has no end marker");
    return;
  }

  const MCSymbol *PrologEnd = Info->PrologEnd ? Info->PrologEnd : Info->Begin;
  std::optional<int64_t> PrologSize =
      getAbsDifference(Streamer, PrologEnd, Info->Begin);
  if (PrologSize && (*PrologSize < 0 || (*PrologSize & 3))) {
    reportFrameError(Streamer, *Info,
                     "prologue end is not at an instruction boundary");
    return;
  }

  uint16_t RequiredState = STATE_Integer;
  SmallVector<EncodedCode, 16> PrologCodes;
  uint16_t PrologCodeSlots = 0;
  if (!encodeProgram(Streamer, *M, *Info, Info->Instructions, Info->Begin,
                     PrologSize ? static_cast<uint32_t>(*PrologSize)
                                : UINT32_MAX,
                     PrologCodes, PrologCodeSlots, RequiredState))
    return;

  SmallVector<EncodedEpilog, 4> Epilogs;
  uint32_t TotalEpilogSlots = 0;
  for (const auto &[Symbol, Epilog] : Info->EpilogMap) {
    if (!Epilog.Start || !Epilog.End) {
      reportFrameError(Streamer, *Info, "epilogue scope is incomplete");
      return;
    }
    std::optional<int64_t> Start =
        getAbsDifference(Streamer, Epilog.Start, Info->Begin);
    std::optional<int64_t> End =
        getAbsDifference(Streamer, Epilog.End, Info->Begin);
    if ((Start && ((*Start & 3) || (PrologSize && *Start < *PrologSize))) ||
        (End && (*End & 3)) || (Start && End && *End <= *Start)) {
      reportFrameError(
          Streamer, *Info,
          "epilogue scope is empty, unaligned, or in the prologue");
      return;
    }
    // A scope without rules describes nothing; leave it out.
    if (Epilog.Instructions.empty())
      continue;
    EncodedEpilog E;
    E.Start = Epilog.Start;
    E.End = Epilog.End;
    E.FirstCodeSlot = static_cast<uint16_t>(TotalEpilogSlots);
    uint32_t Length =
        Start && End ? static_cast<uint32_t>(*End - *Start) : UINT32_MAX;
    if (!encodeProgram(Streamer, *M, *Info, Epilog.Instructions, Epilog.Start,
                       Length, E.Codes, E.CodeSlots, RequiredState))
      return;
    TotalEpilogSlots += E.CodeSlots;
    if (TotalEpilogSlots > UINT16_MAX) {
      reportFrameError(Streamer, *Info, "epilogue programs exceed 65535 slots");
      return;
    }
    Epilogs.push_back(std::move(E));
  }
  if (Epilogs.size() > UINT16_MAX) {
    reportFrameError(Streamer, *Info, "too many epilogue scopes");
    return;
  }

  uint8_t EstablisherRegister = 0;
  int32_t EstablisherFrameOffset = 0;
  if (Info->Establisher) {
    EstablisherRegister = Info->Establisher->first;
    EstablisherFrameOffset = Info->Establisher->second;
  }

  uint32_t Flags = 0;
  if (Info->HandlesExceptions)
    Flags |= UNW_ExceptionHandler;
  if (Info->HandlesUnwind)
    Flags |= UNW_TerminateHandler;
  if (Info->ChainedParent)
    Flags |= UNW_ChainInfo;

  // A function whose rules are the entry state everywhere needs no record:
  // its .pdata entry carries the packed frameless form.
  if (PrologCodes.empty() && Epilogs.empty() && Flags == 0 &&
      !Info->ExceptionHandler && !EstablisherRegister &&
      !EstablisherFrameOffset && !ChainParents.count(Info)) {
    PackedFrames.insert(Info);
    return;
  }

  uint64_t RecordSize = UnwindHeaderSize;
  RecordSize += uint64_t(PrologCodeSlots) * UnwindCodeSize;
  RecordSize += uint64_t(Epilogs.size()) * EpilogScopeSize;
  RecordSize += uint64_t(TotalEpilogSlots) * UnwindCodeSize;
  if (Info->ChainedParent)
    RecordSize += RuntimeFunctionSize;
  else if (Info->ExceptionHandler)
    RecordSize += HandlerSize;

  MCSection *XData = Streamer.getAssociatedXDataSection(Info->TextSection);
  Streamer.switchSection(XData);
  Streamer.emitValueToAlignment(Align(4));
  MCSymbol *Record = Streamer.getContext().createTempSymbol("ntuw");
  Streamer.emitLabel(Record);
  Info->Symbol = Record;

  Streamer.emitInt32(UnwindMagic);
  Streamer.emitInt16(UnwindHeaderSize);
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
  emitDifference32(Streamer, FunctionEnd, Info->Begin);

  for (const EncodedCode &Code : PrologCodes)
    emitCode(Streamer, Code);
  for (const EncodedEpilog &Epilog : Epilogs) {
    emitDifference32(Streamer, Epilog.Start, Info->Begin);
    emitDifference32(Streamer, Epilog.End, Info->Begin);
    Streamer.emitInt16(Epilog.FirstCodeSlot);
    Streamer.emitInt16(Epilog.CodeSlots);
    Streamer.emitInt32(0);
  }
  for (const EncodedEpilog &Epilog : Epilogs)
    for (const EncodedCode &Code : Epilog.Codes)
      emitCode(Streamer, Code);

  if (Info->ChainedParent) {
    emitImageRelative32(Streamer, Info->ChainedParent->Begin);
    emitImageRelative32(Streamer, Info->ChainedParent->Symbol);
  } else if (Info->ExceptionHandler) {
    emitImageRelative32(Streamer, Info->ExceptionHandler);
    auto It = HandlerDataSymbols.find(Info);
    if (It != HandlerDataSymbols.end())
      emitImageRelative32(Streamer, It->second);
    else
      Streamer.emitInt32(0);
  }
}

void Win64EH::NTUWUnwindEmitter::Emit(MCStreamer &Streamer) const {
  for (const auto &Frame : Streamer.getWinFrameInfos())
    if (Frame->ChainedParent)
      ChainParents.insert(Frame->ChainedParent);
  for (const auto &Frame : Streamer.getWinFrameInfos())
    emitRecord(Streamer, Frame.get());

  MCContext &Context = Streamer.getContext();
  for (const auto &Frame : Streamer.getWinFrameInfos()) {
    bool Packed = PackedFrames.count(Frame.get());
    if (!Frame->Symbol && !Packed)
      continue;
    MCSection *PData = Streamer.getAssociatedPDataSection(Frame->TextSection);
    Streamer.switchSection(PData);
    Streamer.emitValueToAlignment(Align(4));
    emitImageRelative32(Streamer, Frame->Begin);
    if (!Packed) {
      emitImageRelative32(Streamer, Frame->Symbol);
      continue;
    }
    // (FunctionLength / 4) << 3 | PACKED_Frameless << 1 | 1, with the length
    // a multiple of 4.
    const MCSymbol *End = Frame->End ? Frame->End : Frame->FuncletOrFuncEnd;
    std::optional<int64_t> Length =
        getAbsDifference(Streamer, End, Frame->Begin);
    if (Length && !encodePackedFrameless(*Length)) {
      reportFrameError(Streamer, *Frame,
                       "the function is too long for a packed entry");
      continue;
    }
    const MCExpr *Diff = MCBinaryExpr::createSub(
        MCSymbolRefExpr::create(End, Context),
        MCSymbolRefExpr::create(Frame->Begin, Context), Context);
    const MCExpr *Word = MCBinaryExpr::createOr(
        MCBinaryExpr::createShl(Diff, MCConstantExpr::create(1, Context),
                                Context),
        MCConstantExpr::create(PackedFlag, Context), Context);
    Streamer.emitValue(Word, 4);
  }
}

void Win64EH::NTUWUnwindEmitter::emitObjectMarker(MCStreamer &Streamer) {
  MCContext &Context = Streamer.getContext();
  MCSymbol *Marker = Context.getOrCreateSymbol(UnwindObjectMarker);
  if (Marker->isDefined())
    return;
  Streamer.beginCOFFSymbolDef(Marker);
  Streamer.emitCOFFSymbolStorageClass(COFF::IMAGE_SYM_CLASS_STATIC);
  Streamer.emitCOFFSymbolType(COFF::IMAGE_SYM_DTYPE_NULL);
  Streamer.endCOFFSymbolDef();
  Streamer.emitAssignment(Marker, MCConstantExpr::create(1, Context));
}
