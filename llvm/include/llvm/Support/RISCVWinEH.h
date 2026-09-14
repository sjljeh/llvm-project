//===-- llvm/Support/RISCVWinEH.h - RISC-V64 RVUW format -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes and validates version 1 of the ReactOS-private RISC-V64
// Windows unwind format (RVUW).  It is not a Microsoft ABI.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RISCVWINEH_H
#define LLVM_SUPPORT_RISCVWINEH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <system_error>

namespace llvm::RISCVWinEH {

constexpr uint32_t UnwindMagic = 0x57555652;
constexpr uint16_t UnwindVersion = 1;
constexpr uint16_t UnwindHeaderSize = 32;
constexpr uint32_t RuntimeFunctionSize = 12;
constexpr uint32_t UnwindCodeSize = 8;
constexpr uint32_t EpilogScopeSize = 16;

enum UnwindFlags : uint32_t {
  UNW_ExceptionHandler = 0x00000001,
  UNW_TerminateHandler = 0x00000002,
  UNW_ChainInfo = 0x00000004,
};

constexpr uint32_t UnwindFlagsMask =
    UNW_ExceptionHandler | UNW_TerminateHandler | UNW_ChainInfo;

enum RequiredState : uint16_t {
  STATE_Integer = 0x0001,
  STATE_Floating = 0x0002,
  STATE_Vector = 0x0004,
};

constexpr uint16_t RequiredStateMask =
    STATE_Integer | STATE_Floating | STATE_Vector;

enum UnwindOpcodes : uint8_t {
  UOP_SetCFA = 0x01,
  UOP_SaveGPR = 0x02,
  UOP_SameGPR = 0x03,
  UOP_GPRFromGPR = 0x04,
  UOP_SaveFPR = 0x05,
  UOP_SameFPR = 0x06,
  UOP_FPRFromFPR = 0x07,
  UOP_SaveFCSR = 0x08,
  UOP_SameFCSR = 0x09,
  UOP_SetCFALarge = 0x10,
  UOP_SaveGPRLarge = 0x11,
  UOP_SaveFPRLarge = 0x12,
  UOP_SaveFCSRLarge = 0x13,
};

constexpr uint32_t OpcodeMask = 0x3f;
constexpr uint32_t RegisterMask = 0x1f;
constexpr unsigned RegisterShift = 6;
constexpr unsigned OperandShift = 11;
constexpr uint32_t OperandMask = 0x1fffff;
constexpr int32_t SmallOperandMin = -(1 << 20);
constexpr int32_t SmallOperandMax = (1 << 20) - 1;

struct RuntimeFunction {
  support::ulittle32_t BeginAddress;
  support::ulittle32_t EndAddress;
  support::ulittle32_t UnwindData;
};

struct UnwindInfoHeaderV1 {
  support::ulittle32_t Magic;
  support::ulittle16_t Version;
  support::ulittle16_t HeaderSize;
  support::ulittle32_t RecordSize;
  support::ulittle32_t Flags;
  support::ulittle32_t PrologEndOffset;
  support::little32_t EstablisherFrameOffset;
  support::ulittle16_t PrologCodeSlots;
  support::ulittle16_t EpilogScopeCount;
  support::ulittle16_t EpilogCodeSlots;
  support::ulittle16_t RequiredState;
};

struct UnwindCodeV1 {
  support::ulittle32_t CodeOffset;
  support::ulittle32_t OpInfo;
};

struct EpilogScopeV1 {
  support::ulittle32_t StartOffset;
  support::ulittle32_t EndOffset;
  support::ulittle16_t FirstCodeSlot;
  support::ulittle16_t CodeSlots;
  support::ulittle32_t Reserved;
};

struct HandlerV1 {
  support::ulittle32_t ExceptionHandlerRVA;
  support::ulittle32_t HandlerDataRVA;
};

static_assert(sizeof(RuntimeFunction) == RuntimeFunctionSize);
static_assert(sizeof(UnwindInfoHeaderV1) == UnwindHeaderSize);
static_assert(sizeof(UnwindCodeV1) == UnwindCodeSize);
static_assert(sizeof(EpilogScopeV1) == EpilogScopeSize);
static_assert(sizeof(HandlerV1) == 8);

inline bool isIntegerNonvolatile(unsigned Reg) {
  return Reg == 8 || Reg == 9 || (Reg >= 18 && Reg <= 27);
}

inline bool isCFARegister(unsigned Reg) {
  return Reg == 2 || isIntegerNonvolatile(Reg);
}

inline bool isRecoverableGPR(unsigned Reg) {
  return Reg == 1 || isIntegerNonvolatile(Reg);
}

inline const char *getOpcodeName(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SetCFA:
    return "SET_CFA";
  case UOP_SaveGPR:
    return "SAVE_GPR";
  case UOP_SameGPR:
    return "SAME_GPR";
  case UOP_GPRFromGPR:
    return "GPR_FROM_GPR";
  case UOP_SaveFPR:
    return "SAVE_FPR";
  case UOP_SameFPR:
    return "SAME_FPR";
  case UOP_FPRFromFPR:
    return "FPR_FROM_FPR";
  case UOP_SaveFCSR:
    return "SAVE_FCSR";
  case UOP_SameFCSR:
    return "SAME_FCSR";
  case UOP_SetCFALarge:
    return "SET_CFA_LARGE";
  case UOP_SaveGPRLarge:
    return "SAVE_GPR_LARGE";
  case UOP_SaveFPRLarge:
    return "SAVE_FPR_LARGE";
  case UOP_SaveFCSRLarge:
    return "SAVE_FCSR_LARGE";
  default:
    return "UNKNOWN";
  }
}

struct DecodedCode {
  uint32_t SlotIndex;
  uint32_t CodeOffset;
  uint32_t OpInfo;
  uint8_t Opcode;
  uint8_t Register;
  int32_t Operand;
  uint8_t SlotCount;
};

struct DecodedEpilogScope {
  uint32_t StartOffset;
  uint32_t EndOffset;
  uint16_t FirstCodeSlot;
  uint16_t CodeSlots;
  SmallVector<DecodedCode, 8> Codes;
};

struct DecodedUnwindInfo {
  uint32_t RecordSize;
  uint32_t Flags;
  uint32_t PrologEndOffset;
  int32_t EstablisherFrameOffset;
  uint16_t PrologCodeSlots;
  uint16_t EpilogScopeCount;
  uint16_t EpilogCodeSlots;
  uint16_t RequiredState;
  SmallVector<DecodedCode, 8> PrologCodes;
  SmallVector<DecodedEpilogScope, 4> EpilogScopes;
  std::optional<HandlerV1> Handler;
  std::optional<RuntimeFunction> Chained;
};

namespace detail {

inline Error invalid(const Twine &Message) {
  return createStringError("invalid RISC-V64 RVUW: " + Message);
}

inline int32_t decodeSigned21(uint32_t Value) {
  Value &= OperandMask;
  if (Value & (1u << 20))
    Value |= ~OperandMask;
  return static_cast<int32_t>(Value);
}

inline unsigned codeDestination(unsigned Opcode, unsigned Register) {
  switch (Opcode) {
  case UOP_SetCFA:
  case UOP_SetCFALarge:
    return 0x100;
  case UOP_SaveGPR:
  case UOP_SameGPR:
  case UOP_GPRFromGPR:
  case UOP_SaveGPRLarge:
    return 0x200 + Register;
  case UOP_SaveFPR:
  case UOP_SameFPR:
  case UOP_FPRFromFPR:
  case UOP_SaveFPRLarge:
    return 0x300 + Register;
  case UOP_SaveFCSR:
  case UOP_SameFCSR:
  case UOP_SaveFCSRLarge:
    return 0x400;
  default:
    return 0;
  }
}

inline Expected<SmallVector<DecodedCode, 8>>
decodeProgram(ArrayRef<uint8_t> Bytes, uint32_t FirstSlot,
              uint32_t SlotCount, uint32_t CodeLimit,
              uint16_t RequiredState) {
  SmallVector<DecodedCode, 8> Codes;
  uint32_t PreviousOffset = 0;
  bool HasPrevious = false;
  for (uint32_t Slot = 0; Slot < SlotCount;) {
    uint64_t AbsoluteSlot = uint64_t(FirstSlot) + Slot;
    uint64_t Offset = AbsoluteSlot * UnwindCodeSize;
    if (Offset + UnwindCodeSize > Bytes.size())
      return invalid("state program is truncated");

    uint32_t CodeOffset = support::endian::read32le(Bytes.data() + Offset);
    uint32_t OpInfo =
        support::endian::read32le(Bytes.data() + Offset + 4);
    unsigned Opcode = OpInfo & OpcodeMask;
    unsigned Register = (OpInfo >> RegisterShift) & RegisterMask;
    uint32_t RawOperand = (OpInfo >> OperandShift) & OperandMask;
    int32_t Operand = decodeSigned21(RawOperand);
    uint8_t UsedSlots = 1;
    bool IsLarge = Opcode == UOP_SetCFALarge ||
                   Opcode == UOP_SaveGPRLarge ||
                   Opcode == UOP_SaveFPRLarge ||
                   Opcode == UOP_SaveFCSRLarge;
    if (IsLarge) {
      if (RawOperand)
        return invalid("large opcode has nonzero inline operand bits");
      if (Slot + 1 >= SlotCount)
        return invalid("large opcode has no extension slot");
      uint64_t ExtensionOffset = Offset + UnwindCodeSize;
      Operand = static_cast<int32_t>(
          support::endian::read32le(Bytes.data() + ExtensionOffset));
      if (support::endian::read32le(Bytes.data() + ExtensionOffset + 4) != 0)
        return invalid("large opcode extension has nonzero reserved word");
      UsedSlots = 2;
    }

    if ((CodeOffset & 1) || CodeOffset > CodeLimit)
      return invalid("state-transition offset is unaligned or out of range");
    if (HasPrevious && CodeOffset < PreviousOffset)
      return invalid("state-transition offsets are not sorted");

    switch (Opcode) {
    case UOP_SetCFA:
    case UOP_SetCFALarge:
      if (!isCFARegister(Register) ||
          Operand == std::numeric_limits<int32_t>::min())
        return invalid("SET_CFA has an invalid register or operand");
      break;
    case UOP_SaveGPR:
    case UOP_SaveGPRLarge:
      if (!isRecoverableGPR(Register) || (Operand & 7))
        return invalid("SAVE_GPR has an invalid register or offset");
      break;
    case UOP_SameGPR:
      if (!isRecoverableGPR(Register) || Operand != 0)
        return invalid("SAME_GPR has an invalid register or operand");
      break;
    case UOP_GPRFromGPR:
      if (!isRecoverableGPR(Register) || RawOperand > 31 ||
          RawOperand == 3 || RawOperand == 4)
        return invalid("GPR_FROM_GPR has an invalid register operand");
      Operand = static_cast<int32_t>(RawOperand);
      break;
    case UOP_SaveFPR:
    case UOP_SaveFPRLarge:
      if (!(RequiredState & STATE_Floating) || (Operand & 7))
        return invalid("SAVE_FPR lacks floating state or has bad alignment");
      break;
    case UOP_SameFPR:
      if (!(RequiredState & STATE_Floating) || Operand != 0)
        return invalid("SAME_FPR lacks floating state or has an operand");
      break;
    case UOP_FPRFromFPR:
      if (!(RequiredState & STATE_Floating) || RawOperand > 31)
        return invalid("FPR_FROM_FPR has an invalid register operand");
      Operand = static_cast<int32_t>(RawOperand);
      break;
    case UOP_SaveFCSR:
    case UOP_SaveFCSRLarge:
      if (!(RequiredState & STATE_Floating) || Register != 0 || (Operand & 3))
        return invalid("SAVE_FCSR has an invalid state, register, or offset");
      break;
    case UOP_SameFCSR:
      if (!(RequiredState & STATE_Floating) || Register != 0 || Operand != 0)
        return invalid("SAME_FCSR has an invalid state or operand");
      break;
    default:
      return invalid("unknown state opcode");
    }

    unsigned Destination = codeDestination(Opcode, Register);
    for (const DecodedCode &Previous : Codes)
      if (Previous.CodeOffset == CodeOffset &&
          codeDestination(Previous.Opcode, Previous.Register) == Destination)
        return invalid("conflicting rules share a code offset");

    Codes.push_back({Slot, CodeOffset, OpInfo, static_cast<uint8_t>(Opcode), static_cast<uint8_t>(Register), Operand, UsedSlots});
    PreviousOffset = CodeOffset;
    HasPrevious = true;
    Slot += UsedSlots;
  }
  return Codes;
}

} // namespace detail

inline Expected<DecodedUnwindInfo>
decodeUnwindInfo(ArrayRef<uint8_t> Data,
                 uint32_t FunctionLength = std::numeric_limits<uint32_t>::max(),
                 bool Relocatable = false) {
  if (Data.size() < UnwindHeaderSize)
    return detail::invalid("header is truncated");

  DecodedUnwindInfo Result;
  uint32_t Magic = support::endian::read32le(Data.data());
  uint16_t Version = support::endian::read16le(Data.data() + 4);
  uint16_t HeaderSize = support::endian::read16le(Data.data() + 6);
  Result.RecordSize = support::endian::read32le(Data.data() + 8);
  Result.Flags = support::endian::read32le(Data.data() + 12);
  Result.PrologEndOffset = support::endian::read32le(Data.data() + 16);
  Result.EstablisherFrameOffset =
      static_cast<int32_t>(support::endian::read32le(Data.data() + 20));
  Result.PrologCodeSlots = support::endian::read16le(Data.data() + 24);
  Result.EpilogScopeCount = support::endian::read16le(Data.data() + 26);
  Result.EpilogCodeSlots = support::endian::read16le(Data.data() + 28);
  Result.RequiredState = support::endian::read16le(Data.data() + 30);

  if (Magic != UnwindMagic)
    return detail::invalid("bad magic");
  if (Version != UnwindVersion)
    return detail::invalid("unsupported version");
  if (HeaderSize != UnwindHeaderSize)
    return detail::invalid("unsupported header size");
  if ((Result.RecordSize & 3) || Result.RecordSize < HeaderSize ||
      Result.RecordSize > Data.size())
    return detail::invalid("record size is unaligned or outside available data");
  if (Result.Flags & ~UnwindFlagsMask)
    return detail::invalid("unknown flag bits");
  if ((Result.Flags & UNW_ChainInfo) &&
      (Result.Flags & (UNW_ExceptionHandler | UNW_TerminateHandler)))
    return detail::invalid("chained record also has handler flags");
  if ((Result.RequiredState & STATE_Integer) == 0 ||
      (Result.RequiredState & ~RequiredStateMask) ||
      (Result.RequiredState & STATE_Vector))
    return detail::invalid("unsupported required-state bits");
  if ((Result.PrologEndOffset & 1) ||
      Result.PrologEndOffset > FunctionLength)
    return detail::invalid("prologue end is unaligned or outside the function");

  uint64_t PrologBytes = uint64_t(Result.PrologCodeSlots) * UnwindCodeSize;
  uint64_t ScopeBytes = uint64_t(Result.EpilogScopeCount) * EpilogScopeSize;
  uint64_t EpilogBytes = uint64_t(Result.EpilogCodeSlots) * UnwindCodeSize;
  uint64_t TailBytes = (Result.Flags & UNW_ChainInfo)
                           ? sizeof(RuntimeFunction)
                           : (Result.Flags &
                              (UNW_ExceptionHandler | UNW_TerminateHandler))
                                 ? sizeof(HandlerV1)
                                 : 0;
  uint64_t ExpectedSize = uint64_t(HeaderSize) + PrologBytes + ScopeBytes +
                          EpilogBytes + TailBytes;
  if (ExpectedSize > UINT32_MAX || alignTo(ExpectedSize, 4) != Result.RecordSize)
    return detail::invalid("record size does not match its arrays and tail");

  ArrayRef<uint8_t> Record = Data.take_front(Result.RecordSize);
  auto Prolog = detail::decodeProgram(
      Record.drop_front(HeaderSize).take_front(PrologBytes), 0,
      Result.PrologCodeSlots, Result.PrologEndOffset, Result.RequiredState);
  if (!Prolog)
    return Prolog.takeError();
  Result.PrologCodes = std::move(*Prolog);

  uint64_t ScopesOffset = uint64_t(HeaderSize) + PrologBytes;
  uint64_t EpilogCodesOffset = ScopesOffset + ScopeBytes;
  ArrayRef<uint8_t> EpilogCodeBytes =
      Record.slice(EpilogCodesOffset, EpilogBytes);
  uint32_t PreviousEnd = 0;
  for (uint32_t Index = 0; Index != Result.EpilogScopeCount; ++Index) {
    uint64_t Offset = ScopesOffset + uint64_t(Index) * EpilogScopeSize;
    DecodedEpilogScope Scope;
    Scope.StartOffset = support::endian::read32le(Record.data() + Offset);
    Scope.EndOffset = support::endian::read32le(Record.data() + Offset + 4);
    Scope.FirstCodeSlot =
        support::endian::read16le(Record.data() + Offset + 8);
    Scope.CodeSlots =
        support::endian::read16le(Record.data() + Offset + 10);
    uint32_t Reserved =
        support::endian::read32le(Record.data() + Offset + 12);

    if ((Scope.StartOffset | Scope.EndOffset) & 1)
      return detail::invalid("epilogue scope is not two-byte aligned");
    if (Scope.StartOffset >= Scope.EndOffset ||
        Scope.EndOffset > FunctionLength)
      return detail::invalid("epilogue scope is empty or outside the function");
    if (Index && Scope.StartOffset < PreviousEnd)
      return detail::invalid("epilogue scopes overlap");
    if (Reserved)
      return detail::invalid("epilogue scope has nonzero reserved bits");
    if (uint32_t(Scope.FirstCodeSlot) + Scope.CodeSlots >
        Result.EpilogCodeSlots)
      return detail::invalid("epilogue program lies outside its code array");

    auto Codes = detail::decodeProgram(
        EpilogCodeBytes, Scope.FirstCodeSlot, Scope.CodeSlots,
        Scope.EndOffset - Scope.StartOffset, Result.RequiredState);
    if (!Codes)
      return Codes.takeError();
    Scope.Codes = std::move(*Codes);
    Result.EpilogScopes.push_back(std::move(Scope));
    PreviousEnd = Result.EpilogScopes.back().EndOffset;
  }

  uint64_t TailOffset = EpilogCodesOffset + EpilogBytes;
  if (Result.Flags & UNW_ChainInfo) {
    RuntimeFunction Chain;
    Chain.BeginAddress = support::endian::read32le(Record.data() + TailOffset);
    Chain.EndAddress = support::endian::read32le(Record.data() + TailOffset + 4);
    Chain.UnwindData = support::endian::read32le(Record.data() + TailOffset + 8);
    if (!Relocatable) {
      if ((uint32_t(Chain.BeginAddress) | uint32_t(Chain.EndAddress)) & 1)
        return detail::invalid("chained function range is not two-byte aligned");
      if (uint32_t(Chain.BeginAddress) >= uint32_t(Chain.EndAddress) ||
          (uint32_t(Chain.UnwindData) & 3))
        return detail::invalid("chained runtime-function entry is malformed");
    }
    Result.Chained = Chain;
  } else if (Result.Flags &
             (UNW_ExceptionHandler | UNW_TerminateHandler)) {
    HandlerV1 Handler;
    Handler.ExceptionHandlerRVA =
        support::endian::read32le(Record.data() + TailOffset);
    Handler.HandlerDataRVA =
        support::endian::read32le(Record.data() + TailOffset + 4);
    if (!Relocatable &&
        (uint32_t(Handler.ExceptionHandlerRVA) == 0 ||
         (uint32_t(Handler.ExceptionHandlerRVA) & 1) ||
         (uint32_t(Handler.HandlerDataRVA) & 3)))
      return detail::invalid("handler descriptor is malformed");
    Result.Handler = Handler;
  }

  return Result;
}

} // namespace llvm::RISCVWinEH

#endif
