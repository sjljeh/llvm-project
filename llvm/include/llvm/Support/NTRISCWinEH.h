//===-- llvm/Support/NTRISCWinEH.h - Alpha/MIPS/PPC NTUW format -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes, validates, and evaluates NTUW, the table-based unwind
// format that LLVM and ReactOS use for 32-bit Alpha (with -mtaso), MIPS and
// PowerPC Windows code built for MSVC 13.0 (VC 7.0) or later. It is not a
// Microsoft ABI.
//
// NTUW is three formats, one per machine, that share a container: the RVUW
// state table (llvm/Support/RISCVWinEH.h) with a 36-byte header that also
// carries the function length, 8-byte image-relative .pdata entries, and
// machine-specific register namespaces.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_NTRISCWINEH_H
#define LLVM_SUPPORT_NTRISCWINEH_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace llvm::NTRISCWinEH {

enum class Machine : uint8_t { Alpha, MIPS, PPC };

/// Returns the NTUW machine for a COFF machine type, if NTUW defines one.
inline std::optional<Machine> getMachine(uint16_t COFFMachine) {
  switch (COFFMachine) {
  case COFF::IMAGE_FILE_MACHINE_ALPHA:
  case COFF::IMAGE_FILE_MACHINE_ALPHA64:
    // AXP64 has the same registers and register images; only its pointers
    // are wider, and NTUW records hold RVAs.
    return Machine::Alpha;
  case COFF::IMAGE_FILE_MACHINE_R4000:
    return Machine::MIPS;
  case COFF::IMAGE_FILE_MACHINE_POWERPC:
    return Machine::PPC;
  default:
    return std::nullopt;
  }
}

inline const char *getMachineName(Machine M) {
  switch (M) {
  case Machine::Alpha:
    return "Alpha";
  case Machine::MIPS:
    return "MIPS";
  case Machine::PPC:
    return "PowerPC";
  }
  return "unknown";
}

constexpr uint32_t UnwindMagic = 0x5755544e; // "NTUW"
constexpr uint16_t UnwindHeaderSize = 36;
constexpr uint32_t RuntimeFunctionSize = 8;
constexpr uint32_t UnwindCodeSize = 8;
constexpr uint32_t EpilogScopeSize = 16;
constexpr uint32_t HandlerSize = 8;
/// The NT 4.0 .pdata entry that a legacy record embeds.
constexpr uint32_t LegacyEntrySize = 20;
/// The absolute symbol that marks an NTUW object.
constexpr const char UnwindObjectMarker[] = "@unwind.00";

enum UnwindFlags : uint32_t {
  UNW_ExceptionHandler = 0x00000001,
  UNW_TerminateHandler = 0x00000002,
  UNW_ChainInfo = 0x00000004,
  /// The record embeds an NT 4.0 .pdata entry; see LegacyRecord.
  UNW_Legacy = 0x00000008,
};

constexpr uint32_t UnwindFlagsMask =
    UNW_ExceptionHandler | UNW_TerminateHandler | UNW_ChainInfo | UNW_Legacy;

enum RequiredState : uint16_t {
  STATE_Integer = 0x0001,
  STATE_Floating = 0x0002,
};

constexpr uint16_t RequiredStateMask = STATE_Integer | STATE_Floating;

/// The opcode numbers are RVUW's. Opcodes below 0x10 occupy one slot;
/// opcodes 0x10-0x1f are followed by an extension slot
/// {int32 operand, uint32 zero} and keep their inline operand zero.
enum UnwindOpcodes : uint8_t {
  UOP_SetCFA = 0x01,
  UOP_SaveGPR = 0x02,
  UOP_SameGPR = 0x03,
  UOP_GPRFromGPR = 0x04,
  UOP_SaveFPR = 0x05,
  UOP_SameFPR = 0x06,
  UOP_FPRFromFPR = 0x07,
  /// The floating-point control register: Alpha fpcr (64-bit image), MIPS
  /// fcsr and PowerPC fpscr (32-bit images).
  UOP_SaveFPCR = 0x08,
  UOP_SameFPCR = 0x09,
  UOP_ReturnFromGPR = 0x0b,
  UOP_MachineFrame = 0x0c,
  UOP_TrapFrame = 0x0d,
  UOP_Context = 0x0e,
  UOP_SetCFALarge = 0x10,
  UOP_SaveGPRLarge = 0x11,
  UOP_SaveFPRLarge = 0x12,
  UOP_SaveFPCRLarge = 0x13,
  /// PowerPC special registers; the Register field is an SPRIndex.
  UOP_SaveSPR = 0x16,
  UOP_SPRFromGPR = 0x17,
  UOP_SameSPR = 0x18,
};

/// The special registers that SAVE_SPR, SPR_FROM_GPR and SAME_SPR name.
enum SPRIndex : uint8_t { SPR_LR = 0, SPR_CR = 1, SPR_CTR = 2 };
constexpr unsigned NumSPRs = 3;

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
  return Opcode >= UOP_SetCFALarge && Opcode <= UOP_SaveFPCRLarge;
}

/// Opcodes that always use the two-slot form, whatever their operand.
inline bool isExtendedOpcode(unsigned Opcode) {
  return Opcode == UOP_SaveSPR || Opcode == UOP_SPRFromGPR ||
         Opcode == UOP_SameSPR;
}

inline unsigned getLargeOpcode(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SetCFA:
    return UOP_SetCFALarge;
  case UOP_SaveGPR:
    return UOP_SaveGPRLarge;
  case UOP_SaveFPR:
    return UOP_SaveFPRLarge;
  case UOP_SaveFPCR:
    return UOP_SaveFPCRLarge;
  default:
    return 0;
  }
}

inline unsigned getBaseOpcode(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SetCFALarge:
    return UOP_SetCFA;
  case UOP_SaveGPRLarge:
    return UOP_SaveGPR;
  case UOP_SaveFPRLarge:
    return UOP_SaveFPR;
  case UOP_SaveFPCRLarge:
    return UOP_SaveFPCR;
  default:
    return Opcode;
  }
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
  case UOP_SaveFPCR:
    return "SAVE_FPCR";
  case UOP_SameFPCR:
    return "SAME_FPCR";
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
  case UOP_SaveFPCRLarge:
    return "SAVE_FPCR_LARGE";
  case UOP_SaveSPR:
    return "SAVE_SPR";
  case UOP_SPRFromGPR:
    return "SPR_FROM_GPR";
  case UOP_SameSPR:
    return "SAME_SPR";
  default:
    return "UNKNOWN";
  }
}

/// The directive spelling of a small opcode, without the ".seh_" prefix.
inline const char *getDirectiveName(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SetCFA:
    return "set_cfa";
  case UOP_SaveGPR:
    return "save_gpr";
  case UOP_SameGPR:
    return "same_gpr";
  case UOP_GPRFromGPR:
    return "gpr_from_gpr";
  case UOP_SaveFPR:
    return "save_fpr";
  case UOP_SameFPR:
    return "same_fpr";
  case UOP_FPRFromFPR:
    return "fpr_from_fpr";
  case UOP_SaveFPCR:
    return "save_fpcr";
  case UOP_SameFPCR:
    return "same_fpcr";
  case UOP_ReturnFromGPR:
    return "return_from";
  case UOP_MachineFrame:
    return "machine_frame";
  case UOP_TrapFrame:
    return "trap_frame";
  case UOP_Context:
    return "context";
  case UOP_SaveSPR:
    return "save_spr";
  case UOP_SPRFromGPR:
    return "spr_from_gpr";
  case UOP_SameSPR:
    return "same_spr";
  default:
    return nullptr;
  }
}

/// Opcodes whose directive takes a register operand.
inline bool directiveHasRegister(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SaveFPCR:
  case UOP_SameFPCR:
  case UOP_MachineFrame:
  case UOP_TrapFrame:
  case UOP_Context:
    return false;
  default:
    return true;
  }
}

/// Opcodes whose directive takes an operand after the register.
inline bool directiveHasOperand(unsigned Opcode) {
  switch (Opcode) {
  case UOP_SameGPR:
  case UOP_SameFPR:
  case UOP_SameFPCR:
  case UOP_SameSPR:
    return false;
  default:
    return true;
  }
}

/// Machine register conventions.
struct MachineInfo {
  /// Width in bytes of a saved general-purpose register image.
  uint8_t GPRSize;
  uint8_t StackPointer;
  /// The default return-address register, or none for PowerPC (LR).
  std::optional<uint8_t> ReturnRegister;
  /// The register that holds zero, if any.
  std::optional<uint8_t> ZeroRegister;
  uint32_t IntegerNonvolatiles;
  uint32_t FloatingNonvolatiles;
  /// Registers the FPR namespace can name.
  uint32_t FloatingRegisters;
  bool HasSPRs;
};

inline MachineInfo getMachineInfo(Machine M) {
  switch (M) {
  case Machine::Alpha:
    // $9-$15 (s0-s5, fp) and $f2-$f9; $30 is sp and $31 and $f31 read zero.
    return {8, 30, 26, 31, 0x0000fe00u, 0x000003fcu, 0x7fffffffu, false};
  case Machine::MIPS:
    // $16-$23 (s0-s7) and $30 (s8); the even FPRs $f20-$f30 are nonvolatile
    // and, with FR=0, name the 64-bit pair they begin.
    return {4, 29, 31, 0, 0x40ff0000u, 0x55500000u, 0x55555555u, false};
  case Machine::PPC:
    // r13-r31 and f14-f31; r1 is sp. The return address is in LR.
    return {4,           1,           std::nullopt, std::nullopt,
            0xffffe000u, 0xffffc000u, 0xffffffffu,  true};
  }
  llvm_unreachable("unknown NTUW machine");
}

inline bool isIntegerNonvolatile(Machine M, unsigned Reg) {
  return Reg < 32 && (getMachineInfo(M).IntegerNonvolatiles >> Reg) & 1;
}

inline bool isFloatingNonvolatile(Machine M, unsigned Reg) {
  return Reg < 32 && (getMachineInfo(M).FloatingNonvolatiles >> Reg) & 1;
}

/// Any integer register except the zero register may hold the CFA base. A
/// volatile base is only meaningful in code that makes no calls.
inline bool isCFARegister(Machine M, unsigned Reg) {
  return Reg < 32 && getMachineInfo(M).ZeroRegister != Reg;
}

/// Registers whose caller value a rule may describe: everything except zero
/// and sp, so that trap frames can restore volatile registers too.
inline bool isRecoverableGPR(Machine M, unsigned Reg) {
  MachineInfo Info = getMachineInfo(M);
  return Reg < 32 && Info.ZeroRegister != Reg && Info.StackPointer != Reg;
}

inline bool isRecoverableFPR(Machine M, unsigned Reg) {
  return Reg < 32 && (getMachineInfo(M).FloatingRegisters >> Reg) & 1;
}

/// A register-relative establisher frame must survive unwinding of the frames
/// it calls: sp or an integer nonvolatile. Zero names no register.
inline bool isEstablisherRegister(Machine M, unsigned Reg) {
  return Reg == getMachineInfo(M).StackPointer || isIntegerNonvolatile(M, Reg);
}

/// Destination key: two rules with the same key may not share a code offset.
inline unsigned getCodeDestination(unsigned Opcode, unsigned Register) {
  switch (getBaseOpcode(Opcode)) {
  case UOP_SetCFA:
    return 0x100;
  case UOP_SaveGPR:
  case UOP_SameGPR:
  case UOP_GPRFromGPR:
    return 0x200 + Register;
  case UOP_SaveFPR:
  case UOP_SameFPR:
  case UOP_FPRFromFPR:
    return 0x300 + Register;
  case UOP_SaveFPCR:
  case UOP_SameFPCR:
    return 0x400;
  case UOP_SaveSPR:
  case UOP_SPRFromGPR:
  case UOP_SameSPR:
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

inline uint16_t getRequiredState(unsigned Opcode) {
  switch (getBaseOpcode(Opcode)) {
  case UOP_SaveFPR:
  case UOP_SameFPR:
  case UOP_FPRFromFPR:
  case UOP_SaveFPCR:
  case UOP_SameFPCR:
  case UOP_TrapFrame:
  case UOP_Context:
    return STATE_Integer | STATE_Floating;
  default:
    return STATE_Integer;
  }
}

/// Checks one rule's register and operand for machine M. Operand is the
/// inline operand, or the extension word of a two-slot opcode. Returns an
/// empty string when the rule is valid.
inline const char *validateRule(Machine M, unsigned Opcode, unsigned Register,
                                int64_t Operand) {
  MachineInfo Info = getMachineInfo(M);
  if (Register > 31)
    return "register number is out of range";
  if (!isInt<32>(Operand))
    return "operand is out of range";
  unsigned GPRAlign = Info.GPRSize;
  unsigned FPRAlign = M == Machine::Alpha ? 8 : 4;
  switch (getBaseOpcode(Opcode)) {
  case UOP_SetCFA:
    if (!isCFARegister(M, Register))
      return "SET_CFA names an invalid base register";
    if (Operand == std::numeric_limits<int32_t>::min())
      return "SET_CFA operand cannot form an establisher-frame offset";
    if (Operand & 3)
      return "SET_CFA offset is not four-byte aligned";
    return "";
  case UOP_SaveGPR:
    if (!isRecoverableGPR(M, Register))
      return "SAVE_GPR names the zero register or sp";
    if (Operand & (GPRAlign - 1))
      return "SAVE_GPR offset is not aligned to the register size";
    return "";
  case UOP_SameGPR:
    if (!isRecoverableGPR(M, Register) || Operand != 0)
      return "SAME_GPR has an invalid register or operand";
    return "";
  case UOP_GPRFromGPR:
    if (!isRecoverableGPR(M, Register) || Operand < 0 || Operand > 31 ||
        Info.ZeroRegister == Operand)
      return "GPR_FROM_GPR has an invalid destination or source register";
    return "";
  case UOP_SaveFPR:
    if (!isRecoverableFPR(M, Register))
      return "SAVE_FPR names an invalid floating-point register";
    if (Operand & (FPRAlign - 1))
      return "SAVE_FPR offset is misaligned";
    return "";
  case UOP_SameFPR:
    if (!isRecoverableFPR(M, Register) || Operand != 0)
      return "SAME_FPR has an invalid register or operand";
    return "";
  case UOP_FPRFromFPR:
    if (!isRecoverableFPR(M, Register) || Operand < 0 ||
        !isRecoverableFPR(M, Operand))
      return "FPR_FROM_FPR has an invalid destination or source register";
    return "";
  case UOP_SaveFPCR:
    if (Register != 0 || (Operand & (M == Machine::Alpha ? 7 : 3)))
      return "SAVE_FPCR has a register or a misaligned offset";
    return "";
  case UOP_SameFPCR:
    if (Register != 0 || Operand != 0)
      return "SAME_FPCR has a register or an operand";
    return "";
  case UOP_ReturnFromGPR:
    // The operand adjusts the link value, in instructions.
    if (!isCFARegister(M, Register) || (Operand & 3))
      return "RETURN_FROM_GPR has an invalid register or adjustment";
    return "";
  case UOP_MachineFrame:
  case UOP_TrapFrame:
  case UOP_Context:
    if (Register != 0 || (Operand & (GPRAlign - 1)))
      return "frame rule has a register or a misaligned offset";
    return "";
  case UOP_SaveSPR:
    if (!Info.HasSPRs || Register >= NumSPRs)
      return "SAVE_SPR names no special register of this machine";
    if (Operand & 3)
      return "SAVE_SPR offset is not four-byte aligned";
    return "";
  case UOP_SPRFromGPR:
    if (!Info.HasSPRs || Register >= NumSPRs || Operand < 0 || Operand > 31)
      return "SPR_FROM_GPR has an invalid special or source register";
    return "";
  case UOP_SameSPR:
    if (!Info.HasSPRs || Register >= NumSPRs || Operand != 0)
      return "SAME_SPR has an invalid special register or an operand";
    return "";
  default:
    return "unknown state opcode";
  }
}

/// The format of an image's exception directory.
enum class TableFormat { Empty, NTUW, Legacy, Invalid };

/// Determines an image's exception directory format from its contents. An
/// image carries no other marker: the directory is NTUW when its size is a
/// multiple of 8 and its first entry's UnwindData is a 4-byte aligned RVA of a
/// record that begins with the NTUW magic and header size; otherwise it is
/// NT 4.0 when its size is a multiple of 20. ReadImage returns the Size bytes
/// at an RVA, or fewer when they lie outside the image. Relocating the image
/// does not change the answer, because only RVAs are followed.
inline TableFormat classifyExceptionDirectory(
    ArrayRef<uint8_t> Directory,
    function_ref<ArrayRef<uint8_t>(uint32_t RVA, uint32_t Size)> ReadImage) {
  if (Directory.empty())
    return TableFormat::Empty;
  if (Directory.size() % RuntimeFunctionSize == 0) {
    uint32_t UnwindData = support::endian::read32le(Directory.data() + 4);
    if ((UnwindData & 3) == 0) {
      ArrayRef<uint8_t> Header = ReadImage(UnwindData, 6);
      if (Header.size() == 6 &&
          support::endian::read32le(Header.data()) == UnwindMagic &&
          support::endian::read16le(Header.data() + 4) == UnwindHeaderSize)
        return TableFormat::NTUW;
    }
  }
  if (Directory.size() % LegacyEntrySize == 0)
    return TableFormat::Legacy;
  return TableFormat::Invalid;
}

struct RuntimeFunction {
  support::ulittle32_t BeginAddress;
  /// The RVA of an .xdata record, or a packed description when bit 0 is set.
  support::ulittle32_t UnwindData;

  bool isPacked() const { return uint32_t(UnwindData) & 1; }
};

static_assert(sizeof(RuntimeFunction) == RuntimeFunctionSize);

struct DecodedCode {
  uint32_t SlotIndex;
  uint32_t CodeOffset;
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

/// The NT 4.0 .pdata entry embedded in an UNW_Legacy record. Its addresses
/// are absolute.
struct LegacyEntry {
  uint32_t BeginAddress;
  uint32_t EndAddress;
  uint32_t ExceptionHandler;
  uint32_t HandlerData;
  uint32_t PrologEndAddress;
};

struct DecodedUnwindInfo {
  uint32_t RecordSize = 0;
  uint32_t Flags = 0;
  uint32_t PrologEndOffset = 0;
  int32_t EstablisherFrameOffset = 0;
  uint16_t PrologCodeSlots = 0;
  uint16_t EpilogScopeCount = 0;
  uint16_t EpilogCodeSlots = 0;
  uint16_t RequiredState = STATE_Integer;
  uint8_t EstablisherRegister = 0;
  uint32_t FunctionLength = 0;
  SmallVector<DecodedCode, 8> PrologCodes;
  SmallVector<DecodedEpilogScope, 4> EpilogScopes;
  /// {ExceptionHandlerRVA, HandlerDataRVA}.
  std::optional<std::pair<uint32_t, uint32_t>> HandlerInfo;
  /// The parent's {BeginAddress, UnwindData}.
  std::optional<std::pair<uint32_t, uint32_t>> Chained;
  std::optional<LegacyEntry> Legacy;
};

namespace detail {

inline Error invalid(Machine M, const Twine &Message) {
  return createStringError(Twine("invalid ") + getMachineName(M) +
                           " NTUW: " + Message);
}

inline int32_t decodeSigned21(uint32_t Value) {
  Value &= OperandMask;
  if (Value & (1u << 20))
    Value |= ~OperandMask;
  return static_cast<int32_t>(Value);
}

inline Expected<SmallVector<DecodedCode, 8>>
decodeProgram(Machine M, ArrayRef<uint8_t> Bytes, uint32_t FirstSlot,
              uint32_t SlotCount, uint32_t CodeLimit, uint16_t RequiredState) {
  SmallVector<DecodedCode, 8> Codes;
  uint32_t PreviousOffset = 0;
  for (uint32_t Slot = 0; Slot < SlotCount;) {
    uint64_t Offset = (uint64_t(FirstSlot) + Slot) * UnwindCodeSize;
    if (Offset + UnwindCodeSize > Bytes.size())
      return invalid(M, "state program is truncated");
    uint32_t CodeOffset = support::endian::read32le(Bytes.data() + Offset);
    uint32_t OpInfo = support::endian::read32le(Bytes.data() + Offset + 4);
    unsigned Opcode = OpInfo & OpcodeMask;
    unsigned Register = (OpInfo >> RegisterShift) & RegisterMask;
    uint32_t RawOperand = (OpInfo >> OperandShift) & OperandMask;
    int32_t Operand = decodeSigned21(RawOperand);
    uint8_t UsedSlots = 1;
    if (isTwoSlotOpcode(Opcode)) {
      if (RawOperand)
        return invalid(M, "two-slot opcode has nonzero inline operand bits");
      if (Slot + 1 >= SlotCount || Offset + 2 * UnwindCodeSize > Bytes.size())
        return invalid(M, "two-slot opcode has no extension slot");
      Operand = static_cast<int32_t>(
          support::endian::read32le(Bytes.data() + Offset + UnwindCodeSize));
      if (support::endian::read32le(Bytes.data() + Offset + UnwindCodeSize +
                                    4) != 0)
        return invalid(M, "extension slot has a nonzero reserved word");
      UsedSlots = 2;
    } else if (Opcode == UOP_GPRFromGPR || Opcode == UOP_FPRFromFPR) {
      Operand = static_cast<int32_t>(RawOperand);
    }

    if ((CodeOffset & 3) || CodeOffset > CodeLimit)
      return invalid(M, "state-transition offset is unaligned or out of range");
    if (!Codes.empty() && CodeOffset < PreviousOffset)
      return invalid(M, "state-transition offsets are not sorted");
    if (StringRef Message = validateRule(M, Opcode, Register, Operand);
        !Message.empty())
      return invalid(M, Message);
    uint16_t Needed = getRequiredState(Opcode);
    if ((RequiredState & Needed) != Needed)
      return invalid(M,
                     Twine(getOpcodeName(Opcode)) +
                         " needs register state the record does not require");
    unsigned Destination = getCodeDestination(Opcode, Register);
    for (const DecodedCode &Previous : Codes)
      if (Previous.CodeOffset == CodeOffset &&
          getCodeDestination(Previous.Opcode, Previous.Register) == Destination)
        return invalid(M, "conflicting rules share a code offset");

    Codes.push_back({Slot, CodeOffset, static_cast<uint8_t>(Opcode),
                     static_cast<uint8_t>(Register), Operand, UsedSlots});
    PreviousOffset = CodeOffset;
    Slot += UsedSlots;
  }
  return Codes;
}

} // namespace detail

/// Decodes and validates an NTUW record. With Relocatable set, fields that
/// relocations fill in (the chain and handler RVAs, the legacy entry) are not
/// checked.
inline Expected<DecodedUnwindInfo>
decodeUnwindInfo(Machine M, ArrayRef<uint8_t> Data, bool Relocatable = false) {
  if (Data.size() < UnwindHeaderSize)
    return detail::invalid(M, "header is truncated");

  DecodedUnwindInfo Result;
  const uint8_t *P = Data.data();
  uint32_t Magic = support::endian::read32le(P);
  uint16_t HeaderSize = support::endian::read16le(P + 4);
  Result.EstablisherRegister = P[6];
  uint8_t Reserved = P[7];
  Result.RecordSize = support::endian::read32le(P + 8);
  Result.Flags = support::endian::read32le(P + 12);
  Result.PrologEndOffset = support::endian::read32le(P + 16);
  Result.EstablisherFrameOffset =
      static_cast<int32_t>(support::endian::read32le(P + 20));
  Result.PrologCodeSlots = support::endian::read16le(P + 24);
  Result.EpilogScopeCount = support::endian::read16le(P + 26);
  Result.EpilogCodeSlots = support::endian::read16le(P + 28);
  Result.RequiredState = support::endian::read16le(P + 30);
  Result.FunctionLength = support::endian::read32le(P + 32);

  if (Magic != UnwindMagic)
    return detail::invalid(M, "bad magic");
  if (HeaderSize != UnwindHeaderSize)
    return detail::invalid(M, "unsupported header size");
  if (Reserved)
    return detail::invalid(M, "header has a nonzero reserved byte");
  if ((Result.RecordSize & 3) || Result.RecordSize < HeaderSize ||
      Result.RecordSize > Data.size())
    return detail::invalid(
        M, "record size is unaligned or outside available data");
  if (Result.Flags & ~UnwindFlagsMask)
    return detail::invalid(M, "unknown flag bits");
  if ((Result.Flags & UNW_ChainInfo) &&
      (Result.Flags &
       (UNW_ExceptionHandler | UNW_TerminateHandler | UNW_Legacy)))
    return detail::invalid(M, "chained record also has handler flags");
  if ((Result.Flags & UNW_Legacy) &&
      (Result.Flags & (UNW_ExceptionHandler | UNW_TerminateHandler)))
    return detail::invalid(M, "legacy record also has handler flags");
  if ((Result.RequiredState & STATE_Integer) == 0 ||
      (Result.RequiredState & ~RequiredStateMask))
    return detail::invalid(M, "unsupported required-state bits");
  if (Result.EstablisherRegister &&
      !isEstablisherRegister(M, Result.EstablisherRegister))
    return detail::invalid(M,
                           "establisher register is not sp or a nonvolatile");
  if (Result.FunctionLength == 0 || (Result.FunctionLength & 3))
    return detail::invalid(M, "function length is zero or unaligned");
  if ((Result.PrologEndOffset & 3) ||
      Result.PrologEndOffset > Result.FunctionLength)
    return detail::invalid(M,
                           "prologue end is unaligned or outside the function");

  uint64_t PrologBytes = uint64_t(Result.PrologCodeSlots) * UnwindCodeSize;
  uint64_t ScopeBytes = uint64_t(Result.EpilogScopeCount) * EpilogScopeSize;
  uint64_t EpilogBytes = uint64_t(Result.EpilogCodeSlots) * UnwindCodeSize;
  uint64_t TailBytes = 0;
  if (Result.Flags & UNW_ChainInfo)
    TailBytes = RuntimeFunctionSize;
  else if (Result.Flags & UNW_Legacy)
    TailBytes = LegacyEntrySize;
  else if (Result.Flags & (UNW_ExceptionHandler | UNW_TerminateHandler))
    TailBytes = HandlerSize;
  if (Result.Flags & UNW_Legacy) {
    if (Result.PrologCodeSlots || Result.EpilogScopeCount ||
        Result.EpilogCodeSlots || Result.EstablisherRegister ||
        Result.EstablisherFrameOffset)
      return detail::invalid(M, "legacy record has a state program");
  }
  uint64_t ExpectedSize =
      uint64_t(HeaderSize) + PrologBytes + ScopeBytes + EpilogBytes + TailBytes;
  if (ExpectedSize > UINT32_MAX ||
      alignTo(ExpectedSize, 4) != Result.RecordSize)
    return detail::invalid(M, "record size does not match its arrays and tail");

  ArrayRef<uint8_t> Record = Data.take_front(Result.RecordSize);
  auto Prolog = detail::decodeProgram(
      M, Record.drop_front(HeaderSize).take_front(PrologBytes), 0,
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
    const uint8_t *S = Record.data() + ScopesOffset + Index * EpilogScopeSize;
    DecodedEpilogScope Scope;
    Scope.StartOffset = support::endian::read32le(S);
    Scope.EndOffset = support::endian::read32le(S + 4);
    Scope.FirstCodeSlot = support::endian::read16le(S + 8);
    Scope.CodeSlots = support::endian::read16le(S + 10);
    if ((Scope.StartOffset | Scope.EndOffset) & 3)
      return detail::invalid(M, "epilogue scope is not four-byte aligned");
    if (Scope.StartOffset < Result.PrologEndOffset ||
        Scope.StartOffset >= Scope.EndOffset ||
        Scope.EndOffset > Result.FunctionLength)
      return detail::invalid(M, "epilogue scope is empty or outside the body");
    if (Index && Scope.StartOffset < PreviousEnd)
      return detail::invalid(M, "epilogue scopes overlap");
    if (support::endian::read32le(S + 12))
      return detail::invalid(M, "epilogue scope has nonzero reserved bits");
    if (uint32_t(Scope.FirstCodeSlot) + Scope.CodeSlots >
        Result.EpilogCodeSlots)
      return detail::invalid(M, "epilogue program lies outside its code array");
    auto Codes = detail::decodeProgram(
        M, EpilogCodeBytes, Scope.FirstCodeSlot, Scope.CodeSlots,
        Scope.EndOffset - Scope.StartOffset, Result.RequiredState);
    if (!Codes)
      return Codes.takeError();
    Scope.Codes = std::move(*Codes);
    PreviousEnd = Scope.EndOffset;
    Result.EpilogScopes.push_back(std::move(Scope));
  }

  const uint8_t *Tail = Record.data() + EpilogCodesOffset + EpilogBytes;
  if (Result.Flags & UNW_ChainInfo) {
    uint32_t Begin = support::endian::read32le(Tail);
    uint32_t Unwind = support::endian::read32le(Tail + 4);
    if (!Relocatable && ((Begin & 3) || (Unwind & 3)))
      return detail::invalid(M, "chained runtime-function entry is malformed");
    Result.Chained = {Begin, Unwind};
  } else if (Result.Flags & UNW_Legacy) {
    LegacyEntry E;
    E.BeginAddress = support::endian::read32le(Tail);
    E.EndAddress = support::endian::read32le(Tail + 4);
    E.ExceptionHandler = support::endian::read32le(Tail + 8);
    E.HandlerData = support::endian::read32le(Tail + 12);
    E.PrologEndAddress = support::endian::read32le(Tail + 16);
    if (!Relocatable &&
        (E.EndAddress < E.BeginAddress ||
         E.EndAddress - E.BeginAddress != Result.FunctionLength ||
         E.PrologEndAddress < E.BeginAddress ||
         E.PrologEndAddress - E.BeginAddress != Result.PrologEndOffset))
      return detail::invalid(M, "legacy entry disagrees with the header");
    Result.Legacy = E;
  } else if (Result.Flags & (UNW_ExceptionHandler | UNW_TerminateHandler)) {
    uint32_t Handler = support::endian::read32le(Tail);
    uint32_t Data = support::endian::read32le(Tail + 4);
    if (!Relocatable && (Handler == 0 || (Handler & 3)))
      return detail::invalid(M, "handler descriptor is malformed");
    Result.HandlerInfo = {Handler, Data};
  }
  return Result;
}

//===----------------------------------------------------------------------===//
// Reference state evaluation
//===----------------------------------------------------------------------===//

enum class RuleKind : uint8_t {
  /// The caller's value is the current value (or the base frame's value).
  Unchanged,
  /// The caller's value is stored at CFA + Offset.
  Saved,
  /// The caller's value is the current value of GPR Source.
  FromRegister,
};

struct RegisterRule {
  RuleKind Kind = RuleKind::Unchanged;
  int32_t Offset = 0;
  uint8_t Source = 0;

  bool operator==(const RegisterRule &Other) const {
    return Kind == Other.Kind && Offset == Other.Offset &&
           Source == Other.Source;
  }
  bool operator!=(const RegisterRule &Other) const { return !(*this == Other); }
};

enum class BaseFrameKind : uint8_t { None, Trap, Context };

/// The effective unwind rules at one code offset.
///
/// To unwind a context C at this offset:
///  1. CFA = C.GPR[CFARegister] + CFAOffset.
///  2. The caller context N starts as C, or as the trap frame or CONTEXT
///     record at CFA + BaseFrameOffset.
///  3. Each GPR, FPR, SPR and FPCR rule overlays N. A saved GPR image is
///     GPRSize bytes, sign-extended on Alpha; a saved FPR is 8 bytes; a
///     saved SPR is 4 bytes.
///  4. With a machine frame, N.pc and N.sp are loaded from
///     CFA + MachineFrameOffset and the following GPRSize bytes. Otherwise,
///     without a base frame, N.sp = CFA and N.pc is the recovered value of
///     the return source (GPR ReturnRegister, or LR on PowerPC) plus
///     ReturnAdjust.
///  5. The caller's ControlPc is exact when hasExactCallerPC() is true.
struct UnwindState {
  uint8_t CFARegister = 0;
  int32_t CFAOffset = 0;
  std::array<RegisterRule, 32> GPR;
  std::array<RegisterRule, 32> FPR;
  std::array<RegisterRule, NumSPRs> SPR;
  RegisterRule FPCR;
  /// The return source: a GPR, or LR when ReturnFromLR is set.
  bool ReturnFromLR = false;
  uint8_t ReturnRegister = 0;
  int32_t ReturnAdjust = 0;
  std::optional<int32_t> MachineFrameOffset;
  BaseFrameKind BaseFrame = BaseFrameKind::None;
  int32_t BaseFrameOffset = 0;

  bool hasExactCallerPC() const {
    return MachineFrameOffset || BaseFrame != BaseFrameKind::None;
  }

  bool operator==(const UnwindState &Other) const {
    return CFARegister == Other.CFARegister && CFAOffset == Other.CFAOffset &&
           GPR == Other.GPR && FPR == Other.FPR && SPR == Other.SPR &&
           FPCR == Other.FPCR && ReturnFromLR == Other.ReturnFromLR &&
           ReturnRegister == Other.ReturnRegister &&
           ReturnAdjust == Other.ReturnAdjust &&
           MachineFrameOffset == Other.MachineFrameOffset &&
           BaseFrame == Other.BaseFrame &&
           BaseFrameOffset == Other.BaseFrameOffset;
  }

  void apply(const DecodedCode &Code) {
    switch (getBaseOpcode(Code.Opcode)) {
    case UOP_SetCFA:
      CFARegister = Code.Register;
      CFAOffset = Code.Operand;
      break;
    case UOP_SaveGPR:
      GPR[Code.Register] = {RuleKind::Saved, Code.Operand, 0};
      break;
    case UOP_SameGPR:
      GPR[Code.Register] = {};
      break;
    case UOP_GPRFromGPR:
      GPR[Code.Register] = {RuleKind::FromRegister, 0,
                            static_cast<uint8_t>(Code.Operand)};
      break;
    case UOP_SaveFPR:
      FPR[Code.Register] = {RuleKind::Saved, Code.Operand, 0};
      break;
    case UOP_SameFPR:
      FPR[Code.Register] = {};
      break;
    case UOP_FPRFromFPR:
      FPR[Code.Register] = {RuleKind::FromRegister, 0,
                            static_cast<uint8_t>(Code.Operand)};
      break;
    case UOP_SaveFPCR:
      FPCR = {RuleKind::Saved, Code.Operand, 0};
      break;
    case UOP_SameFPCR:
      FPCR = {};
      break;
    case UOP_SaveSPR:
      SPR[Code.Register] = {RuleKind::Saved, Code.Operand, 0};
      break;
    case UOP_SPRFromGPR:
      SPR[Code.Register] = {RuleKind::FromRegister, 0,
                            static_cast<uint8_t>(Code.Operand)};
      break;
    case UOP_SameSPR:
      SPR[Code.Register] = {};
      break;
    case UOP_ReturnFromGPR:
      ReturnFromLR = false;
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

/// The state at a function's first instruction: the CFA is sp and the return
/// address is in the machine's link register.
inline UnwindState getEntryState(Machine M) {
  MachineInfo Info = getMachineInfo(M);
  UnwindState State;
  State.CFARegister = Info.StackPointer;
  if (Info.ReturnRegister)
    State.ReturnRegister = *Info.ReturnRegister;
  else
    State.ReturnFromLR = true;
  return State;
}

/// Returns the state that holds at Offset bytes from the function start.
/// Offset may equal the function length (the return address of a call that
/// ends the function). Initial supplies the state at the function start: the
/// entry state, or the parent's body state for a chained record.
inline UnwindState evaluateState(const DecodedUnwindInfo &Info, uint32_t Offset,
                                 const UnwindState &Initial) {
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

/// The establisher frame of a function whose body state is BodyState,
/// given the value of each GPR in the frame's own context.
inline uint64_t getEstablisherFrame(const DecodedUnwindInfo &Info,
                                    const UnwindState &BodyState,
                                    ArrayRef<uint64_t> GPRs) {
  uint64_t Base = Info.EstablisherRegister
                      ? GPRs[Info.EstablisherRegister]
                      : GPRs[BodyState.CFARegister] + BodyState.CFAOffset;
  return Base + int64_t(Info.EstablisherFrameOffset);
}

/// Encodes one rule into its one or two slots.
inline SmallVector<std::pair<uint32_t, uint32_t>, 2>
encodeRule(uint32_t CodeOffset, unsigned Opcode, unsigned Register,
           int32_t Operand) {
  SmallVector<std::pair<uint32_t, uint32_t>, 2> Slots;
  if (isExtendedOpcode(Opcode)) {
    Slots.push_back({CodeOffset, Opcode | (Register << RegisterShift)});
    Slots.push_back({uint32_t(Operand), 0});
  } else if (Operand < SmallOperandMin || Operand > SmallOperandMax) {
    Slots.push_back(
        {CodeOffset, getLargeOpcode(Opcode) | (Register << RegisterShift)});
    Slots.push_back({uint32_t(Operand), 0});
  } else {
    Slots.push_back(
        {CodeOffset, Opcode | (Register << RegisterShift) |
                         ((uint32_t(Operand) & OperandMask) << OperandShift)});
  }
  return Slots;
}

} // namespace llvm::NTRISCWinEH

#endif
