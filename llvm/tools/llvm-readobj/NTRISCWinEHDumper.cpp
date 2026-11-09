//===- NTRISCWinEHDumper.cpp - Alpha/MIPS/PowerPC NTUW printer ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NTRISCWinEHDumper.h"
#include "llvm-readobj.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/NTRISCWinEH.h"

using namespace llvm;
using namespace llvm::object;

namespace llvm::NTRISCWinEH {
namespace {

struct LocatedData {
  ArrayRef<uint8_t> Bytes;
  const coff_section *Section = nullptr;
  uint64_t SectionOffset = 0;
};

bool isImage(const COFFObjectFile &Obj) {
  return Obj.getPE32Header() || Obj.getPE32PlusHeader();
}

std::optional<SymbolRef> resolveSymbol(const Dumper::Context &Ctx,
                                       const coff_section *Section,
                                       uint64_t Offset) {
  SymbolRef Symbol;
  if (Ctx.ResolveSymbol(Section, Offset, Symbol, Ctx.UserData))
    return std::nullopt;
  return Symbol;
}

std::string symbolName(SymbolRef Symbol) {
  Expected<StringRef> Name = Symbol.getName();
  if (!Name) {
    consumeError(Name.takeError());
    return {};
  }
  return Name->str();
}

void printAddress(ScopedPrinter &SW, const Dumper::Context &Ctx,
                  StringRef Label, const coff_section *Section,
                  uint64_t RelocationOffset, uint32_t Value) {
  if (Section)
    if (std::optional<SymbolRef> Symbol =
            resolveSymbol(Ctx, Section, RelocationOffset)) {
      std::string Name = symbolName(*Symbol);
      if (!Name.empty()) {
        SW.printSymbolOffset(Label, Name, Value);
        return;
      }
    }
  SW.printHex(Label, Value);
}

Expected<LocatedData> locateRelocatedData(const Dumper::Context &Ctx,
                                          const coff_section *Source,
                                          uint64_t RelocationOffset,
                                          uint32_t Value) {
  std::optional<SymbolRef> Symbol =
      resolveSymbol(Ctx, Source, RelocationOffset);
  if (!Symbol)
    return createStringError("NTUW data address has no relocation");
  Expected<section_iterator> SectionIt = Symbol->getSection();
  if (!SectionIt)
    return SectionIt.takeError();
  if (*SectionIt == Ctx.COFF.section_end())
    return createStringError("NTUW relocation names no section");
  const coff_section *Section = Ctx.COFF.getCOFFSection(**SectionIt);
  Expected<uint64_t> Address = Symbol->getAddress();
  if (!Address)
    return Address.takeError();
  uint64_t Offset = *Address + Value;
  ArrayRef<uint8_t> Contents;
  if (Error E = Ctx.COFF.getSectionContents(Section, Contents))
    return std::move(E);
  if (Offset > Contents.size())
    return createStringError("NTUW section offset is outside its section");
  return LocatedData{Contents.drop_front(Offset), Section, Offset};
}

Expected<LocatedData> locateUnwindData(const Dumper::Context &Ctx,
                                       const coff_section *PData,
                                       uint64_t RelocationOffset,
                                       uint32_t UnwindRVA) {
  if (!isImage(Ctx.COFF))
    return locateRelocatedData(Ctx, PData, RelocationOffset, UnwindRVA);
  ArrayRef<uint8_t> Header;
  if (Error E = Ctx.COFF.getRvaAndSizeAsBytes(UnwindRVA, UnwindHeaderSize,
                                              Header, "NTUW header"))
    return std::move(E);
  uint32_t RecordSize = support::endian::read32le(Header.data() + 8);
  ArrayRef<uint8_t> Record;
  if (Error E = Ctx.COFF.getRvaAndSizeAsBytes(
          UnwindRVA, std::max(RecordSize, uint32_t(UnwindHeaderSize)), Record,
          "NTUW record"))
    return std::move(E);
  return LocatedData{Record, nullptr, UnwindRVA};
}

std::string gprName(Machine M, unsigned Reg) {
  switch (M) {
  case Machine::Alpha:
  case Machine::MIPS:
    return formatv("${0}", Reg).str();
  case Machine::PPC:
    return formatv("r{0}", Reg).str();
  }
  return {};
}

std::string fprName(Machine M, unsigned Reg) {
  return M == Machine::PPC ? formatv("f{0}", Reg).str()
                           : formatv("$f{0}", Reg).str();
}

const char *sprName(unsigned Index) {
  switch (Index) {
  case SPR_LR:
    return "lr";
  case SPR_CR:
    return "cr";
  case SPR_CTR:
    return "ctr";
  default:
    return "?";
  }
}

void printCode(ScopedPrinter &SW, Machine M, const DecodedCode &Code) {
  DictScope D(SW, "Code");
  SW.printHex("CodeOffset", Code.CodeOffset);
  SW.printString("Opcode", getOpcodeName(Code.Opcode));
  unsigned Base = getBaseOpcode(Code.Opcode);
  switch (Base) {
  case UOP_SetCFA:
  case UOP_SaveGPR:
  case UOP_SameGPR:
  case UOP_GPRFromGPR:
  case UOP_ReturnFromGPR:
    SW.printNumber("Register", gprName(M, Code.Register),
                   unsigned(Code.Register));
    break;
  case UOP_SaveFPR:
  case UOP_SameFPR:
  case UOP_FPRFromFPR:
    SW.printNumber("Register", fprName(M, Code.Register),
                   unsigned(Code.Register));
    break;
  case UOP_SaveSPR:
  case UOP_SPRFromGPR:
  case UOP_SameSPR:
    SW.printNumber("Register", sprName(Code.Register), unsigned(Code.Register));
    break;
  default:
    break;
  }
  if (Base == UOP_GPRFromGPR || Base == UOP_SPRFromGPR)
    SW.printNumber("SourceRegister", gprName(M, Code.Operand),
                   unsigned(Code.Operand));
  else if (Base == UOP_FPRFromFPR)
    SW.printNumber("SourceRegister", fprName(M, Code.Operand),
                   unsigned(Code.Operand));
  else if (directiveHasOperand(Base))
    SW.printNumber("Operand", Code.Operand);
}

void printScopeTable(ScopedPrinter &SW, const Dumper::Context &Ctx,
                     const LocatedData &Data) {
  if (Data.Bytes.size() < 4) {
    SW.printString("ScopeTableError", "truncated count");
    return;
  }
  uint32_t Count = support::endian::read32le(Data.Bytes.data());
  if (4 + uint64_t(Count) * 16 > Data.Bytes.size()) {
    SW.printString("ScopeTableError", "records exceed available data");
    return;
  }
  DictScope Table(SW, "CSpecificScopeTable");
  SW.printNumber("Count", Count);
  ListScope Records(SW, "Scopes");
  for (uint32_t Index = 0; Index != Count; ++Index) {
    uint64_t Offset = 4 + uint64_t(Index) * 16;
    DictScope Scope(SW, "Scope");
    const char *Names[] = {"BeginAddress", "EndAddress", "HandlerAddress",
                           "JumpTarget"};
    for (unsigned Field = 0; Field != 4; ++Field)
      printAddress(
          SW, Ctx, Names[Field], Data.Section,
          Data.SectionOffset + Offset + 4 * Field,
          support::endian::read32le(Data.Bytes.data() + Offset + 4 * Field));
  }
}

void printUnwindInfo(ScopedPrinter &SW, const Dumper::Context &Ctx, Machine M,
                     const LocatedData &Data) {
  DictScope UI(SW, "UnwindInfo");
  if (Data.Bytes.size() < UnwindHeaderSize) {
    SW.printString("ValidationError", "NTUW header is truncated");
    return;
  }
  const uint8_t *P = Data.Bytes.data();
  SW.printHex("Magic", support::endian::read32le(P));
  SW.printNumber("HeaderSize", support::endian::read16le(P + 4));
  SW.printNumber("RecordSize", support::endian::read32le(P + 8));
  SW.printHex("Flags", support::endian::read32le(P + 12));
  SW.printHex("FunctionLength", support::endian::read32le(P + 32));
  SW.printHex("PrologEndOffset", support::endian::read32le(P + 16));
  if (unsigned Establisher = P[6])
    SW.printNumber("EstablisherRegister", gprName(M, Establisher), Establisher);
  SW.printNumber("EstablisherFrameOffset",
                 static_cast<int32_t>(support::endian::read32le(P + 20)));
  SW.printHex("RequiredState", support::endian::read16le(P + 30));

  bool Relocatable = !isImage(Ctx.COFF);
  Expected<DecodedUnwindInfo> Decoded =
      decodeUnwindInfo(M, Data.Bytes, Relocatable);
  if (!Decoded) {
    SW.printString("ValidationError", toString(Decoded.takeError()));
    return;
  }

  {
    ListScope Codes(SW, "PrologCodes");
    for (const DecodedCode &Code : Decoded->PrologCodes)
      printCode(SW, M, Code);
  }
  {
    ListScope Scopes(SW, "EpilogScopes");
    for (const DecodedEpilogScope &Scope : Decoded->EpilogScopes) {
      DictScope S(SW, "EpilogScope");
      SW.printHex("StartOffset", Scope.StartOffset);
      SW.printHex("EndOffset", Scope.EndOffset);
      ListScope Codes(SW, "Codes");
      for (const DecodedCode &Code : Scope.Codes)
        printCode(SW, M, Code);
    }
  }

  uint64_t TailOffset = Decoded->RecordSize;
  if (Decoded->HandlerInfo) {
    TailOffset -= HandlerSize;
    DictScope H(SW, "Handler");
    bool IsCSpecificHandler = false;
    if (Data.Section)
      if (std::optional<SymbolRef> Symbol =
              resolveSymbol(Ctx, Data.Section, Data.SectionOffset + TailOffset))
        IsCSpecificHandler = symbolName(*Symbol) == "__C_specific_handler";
    printAddress(SW, Ctx, "ExceptionHandler", Data.Section,
                 Data.SectionOffset + TailOffset, Decoded->HandlerInfo->first);
    printAddress(SW, Ctx, "HandlerData", Data.Section,
                 Data.SectionOffset + TailOffset + 4,
                 Decoded->HandlerInfo->second);
    // Handler data is personality-specific; decode only scope tables that
    // the relocation identifies as __C_specific_handler's.
    if (IsCSpecificHandler) {
      Expected<LocatedData> HandlerData = locateRelocatedData(
          Ctx, Data.Section, Data.SectionOffset + TailOffset + 4,
          Decoded->HandlerInfo->second);
      if (HandlerData)
        printScopeTable(SW, Ctx, *HandlerData);
      else
        consumeError(HandlerData.takeError());
    }
  } else if (Decoded->Chained) {
    TailOffset -= RuntimeFunctionSize;
    DictScope C(SW, "Chained");
    printAddress(SW, Ctx, "BeginAddress", Data.Section,
                 Data.SectionOffset + TailOffset, Decoded->Chained->first);
    printAddress(SW, Ctx, "UnwindData", Data.Section,
                 Data.SectionOffset + TailOffset + 4, Decoded->Chained->second);
  } else if (Decoded->Legacy) {
    DictScope L(SW, "LegacyEntry");
    SW.printHex("BeginAddress", Decoded->Legacy->BeginAddress);
    SW.printHex("EndAddress", Decoded->Legacy->EndAddress);
    SW.printHex("ExceptionHandler", Decoded->Legacy->ExceptionHandler);
    SW.printHex("HandlerData", Decoded->Legacy->HandlerData);
    SW.printHex("PrologEndAddress", Decoded->Legacy->PrologEndAddress);
  }
}

void printLegacyEntries(ScopedPrinter &SW, const Dumper::Context &Ctx,
                        const coff_section *PData, ArrayRef<uint8_t> Contents) {
  if (Contents.size() % LegacyEntrySize) {
    DictScope Bad(SW, "RuntimeFunction");
    SW.printString("ValidationError",
                   "NT 4.0 .pdata size is not a multiple of 20");
    return;
  }
  const char *Names[] = {"BeginAddress", "EndAddress", "ExceptionHandler",
                         "HandlerData", "PrologEndAddress"};
  for (uint64_t Offset = 0; Offset != Contents.size();
       Offset += LegacyEntrySize) {
    DictScope RF(SW, "RuntimeFunction");
    SW.printString("Format", "NT 4.0");
    for (unsigned Field = 0; Field != 5; ++Field)
      printAddress(
          SW, Ctx, Names[Field], isImage(Ctx.COFF) ? nullptr : PData,
          Offset + 4 * Field,
          support::endian::read32le(Contents.data() + Offset + 4 * Field));
  }
}

} // namespace

bool Dumper::usesNTUW(const COFFObjectFile &COFF) {
  if (isImage(COFF)) {
    // Images carry no marker; the exception directory describes itself.
    const data_directory *Dir = COFF.getDataDirectory(COFF::EXCEPTION_TABLE);
    ArrayRef<uint8_t> Directory;
    if (!Dir || !Dir->Size)
      return false;
    if (Error E = COFF.getRvaAndSizeAsBytes(Dir->RelativeVirtualAddress,
                                            Dir->Size, Directory)) {
      consumeError(std::move(E));
      return false;
    }
    auto Read = [&](uint32_t RVA, uint32_t Size) -> ArrayRef<uint8_t> {
      ArrayRef<uint8_t> Bytes;
      if (Error E = COFF.getRvaAndSizeAsBytes(RVA, Size, Bytes)) {
        consumeError(std::move(E));
        return {};
      }
      return Bytes;
    };
    return classifyExceptionDirectory(Directory, Read) == TableFormat::NTUW;
  }
  for (const SymbolRef &Symbol : COFF.symbols()) {
    Expected<StringRef> Name = Symbol.getName();
    if (!Name) {
      consumeError(Name.takeError());
      continue;
    }
    if (*Name == UnwindObjectMarker)
      return true;
  }
  return false;
}

void Dumper::printData(const Context &Ctx) {
  std::optional<Machine> M = getMachine(Ctx.COFF.getMachine());
  if (!M)
    return;
  bool NTUW = usesNTUW(Ctx.COFF);
  if (!NTUW && Ctx.COFF.getMachine() == COFF::IMAGE_FILE_MACHINE_ALPHA64) {
    // AXP64's own function table format is not decoded.
    SW.printString("UnwindFormat", "AXP64");
    return;
  }
  SW.printString("UnwindFormat", NTUW ? "NTUW" : "NT 4.0");

  for (const SectionRef &SectionRef : Ctx.COFF.sections()) {
    Expected<StringRef> NameOrErr = SectionRef.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    if (*NameOrErr != ".pdata" && !NameOrErr->starts_with(".pdata$"))
      continue;

    const coff_section *PData = Ctx.COFF.getCOFFSection(SectionRef);
    ArrayRef<uint8_t> Contents;
    if (Error E = Ctx.COFF.getSectionContents(PData, Contents))
      reportError(std::move(E), Ctx.COFF.getFileName());
    // Images round .pdata up to the file alignment; the exception directory
    // gives its real size.
    if (isImage(Ctx.COFF))
      if (const data_directory *Dir =
              Ctx.COFF.getDataDirectory(COFF::EXCEPTION_TABLE);
          Dir && Dir->Size && Dir->Size <= Contents.size() &&
          Dir->RelativeVirtualAddress == PData->VirtualAddress)
        Contents = Contents.take_front(Dir->Size);
    if (!NTUW) {
      printLegacyEntries(SW, Ctx, PData, Contents);
      continue;
    }
    if (Contents.size() % RuntimeFunctionSize) {
      DictScope Bad(SW, "RuntimeFunction");
      SW.printString("ValidationError",
                     "NTUW .pdata size is not a multiple of 8");
      continue;
    }
    for (uint64_t Offset = 0; Offset != Contents.size();
         Offset += RuntimeFunctionSize) {
      uint32_t Begin = support::endian::read32le(Contents.data() + Offset);
      uint32_t Unwind = support::endian::read32le(Contents.data() + Offset + 4);
      DictScope RF(SW, "RuntimeFunction");
      const coff_section *Relocated = isImage(Ctx.COFF) ? nullptr : PData;
      printAddress(SW, Ctx, "BeginAddress", Relocated, Offset, Begin);
      printAddress(SW, Ctx, "UnwindData", Relocated, Offset + 4, Unwind);
      if (Unwind & PackedFlag) {
        Expected<DecodedUnwindInfo> Packed = decodePackedUnwindData(*M, Unwind);
        if (!Packed) {
          SW.printString("ValidationError", toString(Packed.takeError()));
          continue;
        }
        SW.printString("Packed", "Frameless");
        SW.printHex("FunctionLength", Packed->FunctionLength);
        continue;
      }
      Expected<LocatedData> Data =
          locateUnwindData(Ctx, PData, Offset + 4, Unwind);
      if (!Data) {
        SW.printString("ValidationError", toString(Data.takeError()));
        continue;
      }
      printUnwindInfo(SW, Ctx, *M, *Data);
    }
  }
}

} // namespace llvm::NTRISCWinEH
