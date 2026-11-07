//===-- llvm/Support/RISCVWinEH.h - RISC-V64 RVUW format -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes, validates, and evaluates the ReactOS RISC-V64 Windows
// unwind format (RVUW). It is not a Microsoft ABI.
//
// An RVUW record is a per-instruction state table: each state transition
// names the code offset after which a rule holds. The prologue program applies
// from the function start up to the prologue end; the body state is the result
// of the whole prologue program; each epilogue scope starts from the body
// state and applies its own program relative to the scope start.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RISCVWINEH_H
#define LLVM_SUPPORT_RISCVWINEH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <system_error>

namespace llvm::RISCVWinEH {

constexpr uint32_t UnwindMagic = 0x57555652; // "RVUW"
constexpr uint16_t UnwindHeaderSize = 32;
constexpr uint32_t RuntimeFunctionSize = 12;
constexpr uint32_t UnwindCodeSize = 8;
constexpr uint32_t EpilogScopeSize = 16;
constexpr uint32_t HandlerSize = 8;

enum UnwindFlags : uint32_t {
  UNW_ExceptionHandler = 0x00000001,
  UNW_TerminateHandler = 0x00000002,
  UNW_ChainInfo = 0x00000004,
};

constexpr uint32_t UnwindFlagsMask =
    UNW_ExceptionHandler | UNW_TerminateHandler | UNW_ChainInfo;

/// Register state an unwinder must be able to read and write in the CONTEXT
/// to apply the record's rules.
enum RequiredState : uint16_t {
  STATE_Integer = 0x0001,
  STATE_Floating = 0x0002,
  STATE_Vector = 0x0004,
};

constexpr uint16_t RequiredStateMask =
    STATE_Integer | STATE_Floating | STATE_Vector;

/// Opcodes below 0x10 occupy one slot. Opcodes 0x10-0x1f are followed by an
/// extension slot {int32 fixed operand, uint32 zero}.
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
  UOP_SameVR = 0x0a,
  UOP_ReturnFromGPR = 0x0b,
  UOP_MachineFrame = 0x0c,
  UOP_TrapFrame = 0x0d,
  UOP_Context = 0x0e,
  UOP_SetCFALarge = 0x10,
  UOP_SaveGPRLarge = 0x11,
  UOP_SaveFPRLarge = 0x12,
  UOP_SaveFCSRLarge = 0x13,
  UOP_SetCFAScaled = 0x14,
  UOP_SaveVR = 0x15,
};

constexpr uint32_t OpcodeMask = 0x3f;
constexpr uint32_t RegisterMask = 0x1f;
constexpr unsigned RegisterShift = 6;
constexpr unsigned OperandShift = 11;
constexpr uint32_t OperandMask = 0x1fffff;
constexpr int32_t SmallOperandMin = -(1 << 20);
constexpr int32_t SmallOperandMax = (1 << 20) - 1;

inline bool isTwoSlotOpcode(unsigned Opcode) {
  return Opcode >= 0x10 && Opcode <= 0x1f;
}

inline bool isLargeOpcode(unsigned Opcode) {
  return Opcode >= UOP_SetCFALarge && Opcode <= UOP_SaveFCSRLarge;
}

inline bool isScaledOpcode(unsigned Opcode) {
  return Opcode == UOP_SetCFAScaled || Opcode == UOP_SaveVR;
}

/// Returns the large form of a small opcode, or 0 if it has none.
inline unsigned getLargeOpcode(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SetCFA:
    return UOP_SetCFALarge;
  case UOP_SaveGPR:
    return UOP_SaveGPRLarge;
  case UOP_SaveFPR:
    return UOP_SaveFPRLarge;
  case UOP_SaveFCSR:
    return UOP_SaveFCSRLarge;
  default:
    return 0;
  }
}

/// Returns the small form of an opcode (the identity for small opcodes).
inline unsigned getBaseOpcode(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SetCFALarge:
    return UOP_SetCFA;
  case UOP_SaveGPRLarge:
    return UOP_SaveGPR;
  case UOP_SaveFPRLarge:
    return UOP_SaveFPR;
  case UOP_SaveFCSRLarge:
    return UOP_SaveFCSR;
  default:
    return Opcode;
  }
}

struct RuntimeFunction {
  support::ulittle32_t BeginAddress;
  support::ulittle32_t EndAddress;
  support::ulittle32_t UnwindData;
};

struct UnwindInfoHeader {
  support::ulittle32_t Magic;
  support::ulittle16_t HeaderSize;
  uint8_t EstablisherRegister;
  uint8_t Reserved;
  support::ulittle32_t RecordSize;
  support::ulittle32_t Flags;
  support::ulittle32_t PrologEndOffset;
  support::little32_t EstablisherFrameOffset;
  support::ulittle16_t PrologCodeSlots;
  support::ulittle16_t EpilogScopeCount;
  support::ulittle16_t EpilogCodeSlots;
  support::ulittle16_t RequiredState;
};

struct UnwindCode {
  support::ulittle32_t CodeOffset;
  support::ulittle32_t OpInfo;
};

struct EpilogScope {
  support::ulittle32_t StartOffset;
  support::ulittle32_t EndOffset;
  support::ulittle16_t FirstCodeSlot;
  support::ulittle16_t CodeSlots;
  support::ulittle32_t Reserved;
};

struct Handler {
  support::ulittle32_t ExceptionHandlerRVA;
  support::ulittle32_t HandlerDataRVA;
};

static_assert(sizeof(RuntimeFunction) == RuntimeFunctionSize);
static_assert(sizeof(UnwindInfoHeader) == UnwindHeaderSize);
static_assert(sizeof(UnwindCode) == UnwindCodeSize);
static_assert(sizeof(EpilogScope) == EpilogScopeSize);
static_assert(sizeof(Handler) == HandlerSize);

/// s0, s1, s2-s11.
inline bool isIntegerNonvolatile(unsigned Reg) {
  return Reg == 8 || Reg == 9 || (Reg >= 18 && Reg <= 27);
}

/// fs0, fs1, fs2-fs11.
inline bool isFloatingNonvolatile(unsigned Reg) {
  return Reg == 8 || Reg == 9 || (Reg >= 18 && Reg <= 27);
}

/// Any integer register except zero, gp, and tp may hold the CFA base. A
/// volatile base is only meaningful in code that makes no calls, such as a
/// stack-probe loop.
inline bool isCFARegister(unsigned Reg) {
  return Reg <= 31 && Reg != 0 && Reg != 3 && Reg != 4;
}

/// Registers whose caller value a rule may describe. The caller sp is the CFA
/// (or comes from a machine, trap, or context frame), so it has no rule.
/// Volatile registers are describable so that trap frames can restore the
/// complete interrupted context.
inline bool isRecoverableGPR(unsigned Reg) {
  return Reg <= 31 && Reg != 0 && Reg != 2;
}

/// The return-address source: ra or any volatile temporary used as a link
/// register (t0 for save/restore libcalls and outlined bodies).
inline bool isReturnRegister(unsigned Reg) { return isCFARegister(Reg); }

/// A register-relative establisher frame must survive unwinding of the frames
/// it calls: sp or an integer nonvolatile.
inline bool isEstablisherRegister(unsigned Reg) {
  return Reg == 2 || isIntegerNonvolatile(Reg);
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
  case UOP_SameVR:
    return "SAME_VR";
  case UOP_ReturnFromGPR:
    return "RETURN_FROM_GPR";
  case UOP_MachineFrame:
    return "MACHINE_FRAME";
  case UOP_TrapFrame:
    return "TRAP_FRAME";
  case UOP_Context:
    return "CONTEXT";
  case UOP_SetCFALarge:
    return "SET_CFA_LARGE";
  case UOP_SaveGPRLarge:
    return "SAVE_GPR_LARGE";
  case UOP_SaveFPRLarge:
    return "SAVE_FPR_LARGE";
  case UOP_SaveFCSRLarge:
    return "SAVE_FCSR_LARGE";
  case UOP_SetCFAScaled:
    return "SET_CFA_SCALED";
  case UOP_SaveVR:
    return "SAVE_VR";
  default:
    return "UNKNOWN";
  }
}

/// The register-file class an opcode's Register field names.
enum class RegisterClass : uint8_t { None, GPR, FPR, VR };

inline RegisterClass getRegisterClass(unsigned Opcode) {
  switch (getBaseOpcode(Opcode)) {
  case UOP_SetCFA:
  case UOP_SaveGPR:
  case UOP_SameGPR:
  case UOP_GPRFromGPR:
  case UOP_ReturnFromGPR:
  case UOP_SetCFAScaled:
    return RegisterClass::GPR;
  case UOP_SaveFPR:
  case UOP_SameFPR:
  case UOP_FPRFromFPR:
    return RegisterClass::FPR;
  case UOP_SaveVR:
  case UOP_SameVR:
    return RegisterClass::VR;
  default:
    return RegisterClass::None;
  }
}

/// Destination key: two rules with the same key may not share a code offset.
inline unsigned getCodeDestination(unsigned Opcode, unsigned Register) {
  switch (getBaseOpcode(Opcode)) {
  case UOP_SetCFA:
  case UOP_SetCFAScaled:
    return 0x100;
  case UOP_SaveGPR:
  case UOP_SameGPR:
  case UOP_GPRFromGPR:
    return 0x200 + Register;
  case UOP_SaveFPR:
  case UOP_SameFPR:
  case UOP_FPRFromFPR:
    return 0x300 + Register;
  case UOP_SaveFCSR:
  case UOP_SameFCSR:
    return 0x400;
  case UOP_SaveVR:
  case UOP_SameVR:
    return 0x500 + Register;
  case UOP_ReturnFromGPR:
    return 0x600;
  case UOP_MachineFrame:
    return 0x700;
  case UOP_TrapFrame:
  case UOP_Context:
    return 0x800;
  default:
    return 0;
  }
}

/// The RequiredState bits an opcode needs.
inline uint16_t getRequiredState(unsigned Opcode) {
  switch (getBaseOpcode(Opcode)) {
  case UOP_SaveFPR:
  case UOP_SameFPR:
  case UOP_FPRFromFPR:
  case UOP_SaveFCSR:
  case UOP_SameFCSR:
  case UOP_TrapFrame:
  case UOP_Context:
    return STATE_Integer | STATE_Floating;
  case UOP_SetCFAScaled:
  case UOP_SaveVR:
  case UOP_SameVR:
    return STATE_Integer | STATE_Vector;
  default:
    return STATE_Integer;
  }
}

/// Checks one rule's register and operands. Operand is the fixed operand
/// (the extension word for two-slot opcodes) and Scale the VLENB multiplier
/// of a scaled rule. Returns an empty string when valid.
inline const char *validateRule(unsigned Opcode, unsigned Register,
                                int64_t Operand, int64_t Scale) {
  if (Register > 31)
    return "register number is out of range";
  if (!isInt<32>(Operand) || !isInt<21>(Scale))
    return "operand is out of range";
  if (!isScaledOpcode(Opcode) && Scale != 0)
    return "only scaled rules take a VLENB multiplier";
  switch (getBaseOpcode(Opcode)) {
  case UOP_SetCFA:
    if (!isCFARegister(Register))
      return "SET_CFA names an invalid base register";
    if (Operand == std::numeric_limits<int32_t>::min())
      return "SET_CFA operand cannot form an establisher-frame offset";
    return "";
  case UOP_SetCFAScaled:
    if (!isCFARegister(Register))
      return "SET_CFA_SCALED names an invalid base register";
    if (Scale == 0)
      return "SET_CFA_SCALED needs a nonzero VLENB multiplier";
    if (Operand == std::numeric_limits<int32_t>::min())
      return "SET_CFA_SCALED operand cannot form an establisher-frame offset";
    return "";
  case UOP_SaveGPR:
    if (!isRecoverableGPR(Register))
      return "SAVE_GPR names zero or sp";
    if (Operand & 7)
      return "SAVE_GPR offset is not eight-byte aligned";
    return "";
  case UOP_SameGPR:
    if (!isRecoverableGPR(Register) || Operand != 0)
      return "SAME_GPR has an invalid register or operand";
    return "";
  case UOP_GPRFromGPR:
    if (!isRecoverableGPR(Register) || Operand < 0 || Operand > 31 ||
        Operand == 3 || Operand == 4)
      return "GPR_FROM_GPR has an invalid destination or source register";
    return "";
  case UOP_SaveFPR:
    if (Operand & 7)
      return "SAVE_FPR offset is not eight-byte aligned";
    return "";
  case UOP_SameFPR:
    if (Operand != 0)
      return "SAME_FPR has an operand";
    return "";
  case UOP_FPRFromFPR:
    if (Operand < 0 || Operand > 31)
      return "FPR_FROM_FPR has an invalid source register";
    return "";
  case UOP_SaveFCSR:
    if (Register != 0 || (Operand & 3))
      return "SAVE_FCSR has a register or a misaligned offset";
    return "";
  case UOP_SameFCSR:
    if (Register != 0 || Operand != 0)
      return "SAME_FCSR has a register or an operand";
    return "";
  case UOP_SaveVR:
    return "";
  case UOP_SameVR:
    if (Operand != 0)
      return "SAME_VR has an operand";
    return "";
  case UOP_ReturnFromGPR:
    // The operand adjusts the link value: -4 names the jalr of a two-word
    // `call t0, sym`, so that the caller is unwound from its pre-call state.
    if (!isReturnRegister(Register) || (Operand & 1))
      return "RETURN_FROM_GPR has an invalid register or an odd adjustment";
    return "";
  case UOP_MachineFrame:
  case UOP_TrapFrame:
  case UOP_Context:
    if (Register != 0 || (Operand & 7))
      return "frame rule has a register or a misaligned offset";
    return "";
  default:
    return "unknown state opcode";
  }
}

struct DecodedCode {
  uint32_t SlotIndex;
  uint32_t CodeOffset;
  uint32_t OpInfo;
  uint8_t Opcode;
  uint8_t Register;
  /// The fixed operand: an offset, a source register, or zero.
  int32_t Operand;
  /// The VLENB multiplier of a scaled rule; zero otherwise.
  int32_t Scale;
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
  uint8_t EstablisherRegister;
  SmallVector<DecodedCode, 8> PrologCodes;
  SmallVector<DecodedEpilogScope, 4> EpilogScopes;
  std::optional<Handler> HandlerInfo;
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
    uint32_t OpInfo = support::endian::read32le(Bytes.data() + Offset + 4);
    unsigned Opcode = OpInfo & OpcodeMask;
    unsigned Register = (OpInfo >> RegisterShift) & RegisterMask;
    uint32_t RawOperand = (OpInfo >> OperandShift) & OperandMask;
    int32_t Operand = decodeSigned21(RawOperand);
    int32_t Scale = 0;
    uint8_t UsedSlots = 1;
    if (isTwoSlotOpcode(Opcode)) {
      if (isLargeOpcode(Opcode) && RawOperand)
        return invalid("large opcode has nonzero inline operand bits");
      if (Slot + 1 >= SlotCount || Offset + 2 * UnwindCodeSize > Bytes.size())
        return invalid("two-slot opcode has no extension slot");
      uint64_t ExtensionOffset = Offset + UnwindCodeSize;
      if (isScaledOpcode(Opcode))
        Scale = Operand;
      Operand = static_cast<int32_t>(
          support::endian::read32le(Bytes.data() + ExtensionOffset));
      if (support::endian::read32le(Bytes.data() + ExtensionOffset + 4) != 0)
        return invalid("extension slot has a nonzero reserved word");
      UsedSlots = 2;
    } else if (Opcode == UOP_GPRFromGPR || Opcode == UOP_FPRFromFPR) {
      Operand = static_cast<int32_t>(RawOperand);
    }

    if ((CodeOffset & 1) || CodeOffset > CodeLimit)
      return invalid("state-transition offset is unaligned or out of range");
    if (HasPrevious && CodeOffset < PreviousOffset)
      return invalid("state-transition offsets are not sorted");
    if (StringRef Message = validateRule(Opcode, Register, Operand, Scale);
        !Message.empty())
      return invalid(Message);
    uint16_t Needed = getRequiredState(Opcode);
    if ((RequiredState & Needed) != Needed)
      return invalid(Twine(getOpcodeName(Opcode)) +
                     " needs register state the record does not require");

    unsigned Destination = getCodeDestination(Opcode, Register);
    for (const DecodedCode &Previous : Codes)
      if (Previous.CodeOffset == CodeOffset &&
          getCodeDestination(Previous.Opcode, Previous.Register) == Destination)
        return invalid("conflicting rules share a code offset");

    Codes.push_back({Slot, CodeOffset, OpInfo, static_cast<uint8_t>(Opcode),
                     static_cast<uint8_t>(Register), Operand, Scale,
                     UsedSlots});
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
  uint16_t HeaderSize = support::endian::read16le(Data.data() + 4);
  Result.EstablisherRegister = Data[6];
  uint8_t Reserved = Data[7];
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
  if (HeaderSize != UnwindHeaderSize)
    return detail::invalid("unsupported header size");
  if (Reserved)
    return detail::invalid("header has a nonzero reserved byte");
  if ((Result.RecordSize & 3) || Result.RecordSize < HeaderSize ||
      Result.RecordSize > Data.size())
    return detail::invalid("record size is unaligned or outside available data");
  if (Result.Flags & ~UnwindFlagsMask)
    return detail::invalid("unknown flag bits");
  if ((Result.Flags & UNW_ChainInfo) &&
      (Result.Flags & (UNW_ExceptionHandler | UNW_TerminateHandler)))
    return detail::invalid("chained record also has handler flags");
  if ((Result.RequiredState & STATE_Integer) == 0 ||
      (Result.RequiredState & ~RequiredStateMask))
    return detail::invalid("unsupported required-state bits");
  if (Result.EstablisherRegister &&
      !isEstablisherRegister(Result.EstablisherRegister))
    return detail::invalid("establisher register is not sp or a nonvolatile");
  if ((Result.PrologEndOffset & 1) ||
      Result.PrologEndOffset > FunctionLength)
    return detail::invalid("prologue end is unaligned or outside the function");

  uint64_t PrologBytes = uint64_t(Result.PrologCodeSlots) * UnwindCodeSize;
  uint64_t ScopeBytes = uint64_t(Result.EpilogScopeCount) * EpilogScopeSize;
  uint64_t EpilogBytes = uint64_t(Result.EpilogCodeSlots) * UnwindCodeSize;
  uint64_t TailBytes =
      (Result.Flags & UNW_ChainInfo) ? sizeof(RuntimeFunction)
      : (Result.Flags & (UNW_ExceptionHandler | UNW_TerminateHandler))
          ? sizeof(Handler)
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
    Scope.CodeSlots = support::endian::read16le(Record.data() + Offset + 10);
    uint32_t Reserved = support::endian::read32le(Record.data() + Offset + 12);

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
  } else if (Result.Flags & (UNW_ExceptionHandler | UNW_TerminateHandler)) {
    Handler H;
    H.ExceptionHandlerRVA =
        support::endian::read32le(Record.data() + TailOffset);
    H.HandlerDataRVA =
        support::endian::read32le(Record.data() + TailOffset + 4);
    if (!Relocatable && (uint32_t(H.ExceptionHandlerRVA) == 0 ||
                         (uint32_t(H.ExceptionHandlerRVA) & 1) ||
                         (uint32_t(H.HandlerDataRVA) & 3)))
      return detail::invalid("handler descriptor is malformed");
    Result.HandlerInfo = H;
  }

  return Result;
}

//===----------------------------------------------------------------------===//
// Reference state evaluation
//===----------------------------------------------------------------------===//

enum class RuleKind : uint8_t {
  /// The caller's value is the current value (or the base frame's value).
  Unchanged,
  /// The caller's value is stored at CFA + Offset + Scale * VLENB.
  Saved,
  /// The caller's value is the current value of register Source.
  FromRegister,
};

struct RegisterRule {
  RuleKind Kind = RuleKind::Unchanged;
  int32_t Offset = 0;
  int32_t Scale = 0;
  uint8_t Source = 0;

  bool operator==(const RegisterRule &Other) const {
    return Kind == Other.Kind && Offset == Other.Offset &&
           Scale == Other.Scale && Source == Other.Source;
  }
};

/// What supplies the caller's register set before individual rules apply.
enum class BaseFrameKind : uint8_t {
  /// An ordinary call: unchanged registers keep their current values.
  None,
  /// An OS-defined KTRAP_FRAME at CFA + BaseFrameOffset.
  Trap,
  /// A complete CONTEXT record at CFA + BaseFrameOffset.
  Context,
};

/// The effective unwind rules at one code offset.
///
/// To unwind a context C at this offset:
///  1. CFA = C.x[CFARegister] + CFAOffset + CFAScale * VLENB.
///  2. The caller context N starts as C, or as the trap frame or CONTEXT
///     record at CFA + BaseFrameOffset.
///  3. Each register rule overlays N.
///  4. With a machine frame, N.pc and N.sp are loaded from
///     CFA + MachineFrameOffset and CFA + MachineFrameOffset + 8. Otherwise,
///     without a base frame, N.pc = N.x[ReturnRegister] + ReturnAdjust and
///     N.sp = CFA.
///  5. The caller ControlPc is exact (not a return address) when
///     hasExactCallerPC() is true.
struct UnwindState {
  uint8_t CFARegister = 2;
  int32_t CFAOffset = 0;
  int32_t CFAScale = 0;
  std::array<RegisterRule, 32> GPR;
  std::array<RegisterRule, 32> FPR;
  std::array<RegisterRule, 32> VR;
  RegisterRule FCSR;
  uint8_t ReturnRegister = 1;
  int32_t ReturnAdjust = 0;
  std::optional<int32_t> MachineFrameOffset;
  BaseFrameKind BaseFrame = BaseFrameKind::None;
  int32_t BaseFrameOffset = 0;

  bool hasExactCallerPC() const {
    return MachineFrameOffset || BaseFrame != BaseFrameKind::None;
  }

  void apply(const DecodedCode &Code) {
    switch (getBaseOpcode(Code.Opcode)) {
    case UOP_SetCFA:
    case UOP_SetCFAScaled:
      CFARegister = Code.Register;
      CFAOffset = Code.Operand;
      CFAScale = Code.Scale;
      break;
    case UOP_SaveGPR:
      GPR[Code.Register] = {RuleKind::Saved, Code.Operand, 0, 0};
      break;
    case UOP_SameGPR:
      GPR[Code.Register] = {};
      break;
    case UOP_GPRFromGPR:
      GPR[Code.Register] = {RuleKind::FromRegister, 0, 0,
                            static_cast<uint8_t>(Code.Operand)};
      break;
    case UOP_SaveFPR:
      FPR[Code.Register] = {RuleKind::Saved, Code.Operand, 0, 0};
      break;
    case UOP_SameFPR:
      FPR[Code.Register] = {};
      break;
    case UOP_FPRFromFPR:
      FPR[Code.Register] = {RuleKind::FromRegister, 0, 0,
                            static_cast<uint8_t>(Code.Operand)};
      break;
    case UOP_SaveFCSR:
      FCSR = {RuleKind::Saved, Code.Operand, 0, 0};
      break;
    case UOP_SameFCSR:
      FCSR = {};
      break;
    case UOP_SaveVR:
      VR[Code.Register] = {RuleKind::Saved, Code.Operand, Code.Scale, 0};
      break;
    case UOP_SameVR:
      VR[Code.Register] = {};
      break;
    case UOP_ReturnFromGPR:
      ReturnRegister = Code.Register;
      ReturnAdjust = Code.Operand;
      break;
    case UOP_MachineFrame:
      MachineFrameOffset = Code.Operand;
      break;
    case UOP_TrapFrame:
      BaseFrame = BaseFrameKind::Trap;
      BaseFrameOffset = Code.Operand;
      break;
    case UOP_Context:
      BaseFrame = BaseFrameKind::Context;
      BaseFrameOffset = Code.Operand;
      break;
    }
  }
};

/// Returns the state that holds at Offset bytes from the function start.
/// Initial supplies the state at the function start (the parent's body state
/// for a chained record); by default the CFA is sp and every rule is
/// Unchanged.
inline UnwindState evaluateState(const DecodedUnwindInfo &Info, uint32_t Offset,
                                 const UnwindState &Initial = UnwindState()) {
  UnwindState State = Initial;
  bool InProlog = Offset < Info.PrologEndOffset;
  for (const DecodedCode &Code : Info.PrologCodes)
    if (!InProlog || Code.CodeOffset <= Offset)
      State.apply(Code);
  if (InProlog)
    return State;
  for (const DecodedEpilogScope &Scope : Info.EpilogScopes) {
    if (Offset < Scope.StartOffset || Offset >= Scope.EndOffset)
      continue;
    for (const DecodedCode &Code : Scope.Codes)
      if (Code.CodeOffset <= Offset - Scope.StartOffset)
        State.apply(Code);
    break;
  }
  return State;
}

} // namespace llvm::RISCVWinEH

#endif
