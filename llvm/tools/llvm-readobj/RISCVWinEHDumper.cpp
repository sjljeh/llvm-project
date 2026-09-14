//===- RISCVWinEHDumper.cpp - RISC-V64 RVUW printer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCVWinEHDumper.h"
#include "llvm-readobj.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/RISCVWinEH.h"

using namespace llvm;
using namespace llvm::object;

namespace llvm::RISCVWinEH {
namespace {

struct LocatedData {
  ArrayRef<uint8_t> Bytes;
  const coff_section *Section = nullptr;
  uint64_t SectionOffset = 0;
};

static bool isImage(const COFFObjectFile &Obj) {
  return Obj.getPE32Header() || Obj.getPE32PlusHeader();
}

static std::optional<SymbolRef> resolveSymbol(const Dumper::Context &Ctx,
                                              const coff_section *Section,
                                              uint64_t Offset) {
  SymbolRef Symbol;
  if (Ctx.ResolveSymbol(Section, Offset, Symbol, Ctx.UserData))
    return std::nullopt;
  return Symbol;
}

static std::string symbolName(SymbolRef Symbol) {
  Expected<StringRef> Name = Symbol.getName();
  if (!Name) {
    consumeError(Name.takeError());
    return {};
  }
  return Name->str();
}

static void printAddress(ScopedPrinter &SW, const Dumper::Context &Ctx,
                         StringRef Label, const coff_section *Section,
                         uint64_t RelocationOffset, uint32_t Value) {
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

static Expected<LocatedData>
locateRelocatedData(const Dumper::Context &Ctx, const coff_section *Source,
                    uint64_t RelocationOffset, uint32_t Value) {
  std::optional<SymbolRef> Symbol =
      resolveSymbol(Ctx, Source, RelocationOffset);
  if (!Symbol)
    return createStringError("RVUW data address has no relocation");

  Expected<section_iterator> SectionIt = Symbol->getSection();
  if (!SectionIt)
    return SectionIt.takeError();
  if (*SectionIt == Ctx.COFF.section_end())
    return createStringError("RVUW relocation names no section");
  const coff_section *Section = Ctx.COFF.getCOFFSection(**SectionIt);
  Expected<uint64_t> Address = Symbol->getAddress();
  if (!Address)
    return Address.takeError();
  if (*Address > UINT64_MAX - Value)
    return createStringError("RVUW section offset overflows");
  uint64_t Offset = *Address + Value;

  ArrayRef<uint8_t> Contents;
  if (Error E = Ctx.COFF.getSectionContents(Section, Contents))
    return std::move(E);
  if (Offset > Contents.size())
    return createStringError("RVUW section offset is outside its section");
  return LocatedData{Contents.drop_front(Offset), Section, Offset};
}

static Expected<LocatedData>
locateUnwindData(const Dumper::Context &Ctx, const coff_section *PData,
                 uint64_t RelocationOffset, uint32_t UnwindRVA) {
  if (!isImage(Ctx.COFF))
    return locateRelocatedData(Ctx, PData, RelocationOffset, UnwindRVA);

  ArrayRef<uint8_t> Header;
  if (Error E = Ctx.COFF.getRvaAndSizeAsBytes(
          UnwindRVA, UnwindHeaderSize, Header, "RISC-V64 RVUW header"))
    return std::move(E);
  uint32_t RecordSize = support::endian::read32le(Header.data() + 8);
  ArrayRef<uint8_t> Record;
  if (Error E = Ctx.COFF.getRvaAndSizeAsBytes(
          UnwindRVA, std::max(RecordSize, uint32_t(UnwindHeaderSize)), Record,
          "RISC-V64 RVUW record"))
    return std::move(E);
  return LocatedData{Record, nullptr, UnwindRVA};
}

static void printCode(ScopedPrinter &SW, const DecodedCode &Code) {
  DictScope D(SW, "Code");
  SW.printNumber("Slot", Code.SlotIndex);
  SW.printHex("CodeOffset", Code.CodeOffset);
  SW.printString("Opcode", getOpcodeName(Code.Opcode));
  SW.printNumber("Register", formatv("x{0}", Code.Register).str(), unsigned(Code.Register));
  if (Code.Opcode == UOP_GPRFromGPR || Code.Opcode == UOP_FPRFromFPR)
    SW.printNumber("SourceRegister", formatv("x{0}", Code.Operand).str(), unsigned(Code.Operand));
  else
    SW.printNumber("Operand", Code.Operand);
  SW.printHex("OpInfo", Code.OpInfo);
  SW.printNumber("Slots", unsigned(Code.SlotCount));
}

static void printScopeTable(ScopedPrinter &SW, const Dumper::Context &Ctx,
                            const LocatedData &Data) {
  if (Data.Bytes.size() < 4) {
    SW.printString("ScopeTableError", "truncated count");
    return;
  }
  uint32_t Count = support::endian::read32le(Data.Bytes.data());
  uint64_t Size = 4 + uint64_t(Count) * 16;
  if (Size > Data.Bytes.size()) {
    SW.printString("ScopeTableError", "records exceed available data");
    return;
  }

  DictScope Table(SW, "CSpecificScopeTable");
  SW.printNumber("Count", Count);
  ListScope Records(SW, "Scopes");
  for (uint32_t Index = 0; Index != Count; ++Index) {
    uint64_t Offset = 4 + uint64_t(Index) * 16;
    DictScope Scope(SW, "Scope");
    SW.printNumber("Index", Index);
    uint32_t Begin = support::endian::read32le(Data.Bytes.data() + Offset);
    uint32_t End = support::endian::read32le(Data.Bytes.data() + Offset + 4);
    uint32_t Handler =
        support::endian::read32le(Data.Bytes.data() + Offset + 8);
    uint32_t Target =
        support::endian::read32le(Data.Bytes.data() + Offset + 12);
    if (Data.Section) {
      printAddress(SW, Ctx, "BeginAddress", Data.Section,
                   Data.SectionOffset + Offset, Begin);
      printAddress(SW, Ctx, "EndAddress", Data.Section,
                   Data.SectionOffset + Offset + 4, End);
      printAddress(SW, Ctx, "HandlerAddress", Data.Section,
                   Data.SectionOffset + Offset + 8, Handler);
      printAddress(SW, Ctx, "JumpTarget", Data.Section,
                   Data.SectionOffset + Offset + 12, Target);
    } else {
      SW.printHex("BeginAddress", Begin);
      SW.printHex("EndAddress", End);
      SW.printHex("HandlerAddress", Handler);
      SW.printHex("JumpTarget", Target);
    }
  }
}

static void printUnwindInfo(ScopedPrinter &SW, const Dumper::Context &Ctx,
                            const LocatedData &Data, uint32_t FunctionLength) {
  DictScope UI(SW, "UnwindInfo");
  if (Data.Bytes.size() < UnwindHeaderSize) {
    SW.printString("ValidationError", "RVUW header is truncated");
    return;
  }

  SW.printHex("Magic", support::endian::read32le(Data.Bytes.data()));
  SW.printNumber("Version", support::endian::read16le(Data.Bytes.data() + 4));
  SW.printNumber("HeaderSize", support::endian::read16le(Data.Bytes.data() + 6));
  SW.printNumber("RecordSize", support::endian::read32le(Data.Bytes.data() + 8));
  SW.printHex("Flags", support::endian::read32le(Data.Bytes.data() + 12));
  SW.printHex("PrologEndOffset", support::endian::read32le(Data.Bytes.data() + 16));
  SW.printNumber("EstablisherFrameOffset", static_cast<int32_t>(support::endian::read32le(Data.Bytes.data() + 20)));
  SW.printNumber("PrologCodeSlots", support::endian::read16le(Data.Bytes.data() + 24));
  SW.printNumber("EpilogScopeCount", support::endian::read16le(Data.Bytes.data() + 26));
  SW.printNumber("EpilogCodeSlots", support::endian::read16le(Data.Bytes.data() + 28));
  SW.printHex("RequiredState", support::endian::read16le(Data.Bytes.data() + 30));

  bool Relocatable = !isImage(Ctx.COFF);
  Expected<DecodedUnwindInfo> Decoded =
      decodeUnwindInfo(Data.Bytes, FunctionLength, Relocatable);
  if (!Decoded) {
    SW.printString("ValidationError", toString(Decoded.takeError()));
    return;
  }

  {
    ListScope Codes(SW, "PrologCodes");
    for (const DecodedCode &Code : Decoded->PrologCodes)
      printCode(SW, Code);
  }
  {
    ListScope Scopes(SW, "EpilogScopes");
    for (const DecodedEpilogScope &Scope : Decoded->EpilogScopes) {
      DictScope S(SW, "EpilogScope");
      SW.printHex("StartOffset", Scope.StartOffset);
      SW.printHex("EndOffset", Scope.EndOffset);
      SW.printNumber("FirstCodeSlot", Scope.FirstCodeSlot);
      SW.printNumber("CodeSlots", Scope.CodeSlots);
      ListScope Codes(SW, "Codes");
      for (const DecodedCode &Code : Scope.Codes)
        printCode(SW, Code);
    }
  }

  uint64_t TailOffset = Decoded->RecordSize;
  if (Decoded->Handler) {
    TailOffset -= sizeof(HandlerV1);
    DictScope H(SW, "Handler");
    uint32_t HandlerRVA = Decoded->Handler->ExceptionHandlerRVA;
    uint32_t HandlerDataRVA = Decoded->Handler->HandlerDataRVA;
    if (Data.Section) {
      printAddress(SW, Ctx, "ExceptionHandler", Data.Section,
                   Data.SectionOffset + TailOffset, HandlerRVA);
      printAddress(SW, Ctx, "HandlerData", Data.Section,
                   Data.SectionOffset + TailOffset + 4, HandlerDataRVA);
    } else {
      SW.printHex("ExceptionHandler", HandlerRVA);
      SW.printHex("HandlerData", HandlerDataRVA);
    }

    Expected<LocatedData> HandlerData =
        Data.Section
            ? locateRelocatedData(Ctx, Data.Section,
                                  Data.SectionOffset + TailOffset + 4,
                                  HandlerDataRVA)
            : [&]() -> Expected<LocatedData> {
                ArrayRef<uint8_t> Contents;
                if (!HandlerDataRVA)
                  return createStringError("handler data RVA is zero");
                if (Error E = Ctx.COFF.getRvaAndSizeAsBytes(
                        HandlerDataRVA, 4, Contents,
                        "RISC-V64 handler data"))
                  return std::move(E);
                uintptr_t Address;
                if (Error E = Ctx.COFF.getRvaPtr(HandlerDataRVA, Address))
                  return std::move(E);
                StringRef File = Ctx.COFF.getData();
                const uint8_t *Ptr = reinterpret_cast<const uint8_t *>(Address);
                const uint8_t *FileEnd =
                    reinterpret_cast<const uint8_t *>(File.end());
                return LocatedData{
                    ArrayRef<uint8_t>(Ptr, static_cast<size_t>(FileEnd - Ptr)),
                    nullptr, HandlerDataRVA};
              }();
    if (HandlerData)
      printScopeTable(SW, Ctx, *HandlerData);
    else
      consumeError(HandlerData.takeError());
  } else if (Decoded->Chained) {
    TailOffset -= sizeof(RuntimeFunction);
    DictScope C(SW, "Chained");
    if (Data.Section) {
      printAddress(SW, Ctx, "BeginAddress", Data.Section,
                   Data.SectionOffset + TailOffset,
                   Decoded->Chained->BeginAddress);
      printAddress(SW, Ctx, "EndAddress", Data.Section,
                   Data.SectionOffset + TailOffset + 4,
                   Decoded->Chained->EndAddress);
      printAddress(SW, Ctx, "UnwindData", Data.Section,
                   Data.SectionOffset + TailOffset + 8,
                   Decoded->Chained->UnwindData);
    } else {
      SW.printHex("BeginAddress", uint32_t(Decoded->Chained->BeginAddress));
      SW.printHex("EndAddress", uint32_t(Decoded->Chained->EndAddress));
      SW.printHex("UnwindData", uint32_t(Decoded->Chained->UnwindData));
    }
  }
}

} // namespace

void Dumper::printData(const Context &Ctx) {
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
    if (Contents.size() % sizeof(RuntimeFunction)) {
      DictScope Bad(SW, "RuntimeFunction");
      SW.printString("ValidationError", "RISC-V64 .pdata size is not a multiple of 12");
      continue;
    }

    for (uint64_t Offset = 0; Offset != Contents.size();
         Offset += sizeof(RuntimeFunction)) {
      RuntimeFunction Entry;
      Entry.BeginAddress =
          support::endian::read32le(Contents.data() + Offset);
      Entry.EndAddress =
          support::endian::read32le(Contents.data() + Offset + 4);
      Entry.UnwindData =
          support::endian::read32le(Contents.data() + Offset + 8);

      DictScope RF(SW, "RuntimeFunction");
      printAddress(SW, Ctx, "BeginAddress", PData, Offset, Entry.BeginAddress);
      printAddress(SW, Ctx, "EndAddress", PData, Offset + 4, Entry.EndAddress);
      printAddress(SW, Ctx, "UnwindData", PData, Offset + 8, Entry.UnwindData);

      uint32_t FunctionLength = std::numeric_limits<uint32_t>::max();
      if (isImage(Ctx.COFF) &&
          uint32_t(Entry.EndAddress) > uint32_t(Entry.BeginAddress))
        FunctionLength =
            uint32_t(Entry.EndAddress) - uint32_t(Entry.BeginAddress);
      Expected<LocatedData> Data =
          locateUnwindData(Ctx, PData, Offset + 8,
                           uint32_t(Entry.UnwindData));
      if (!Data) {
        SW.printString("ValidationError", toString(Data.takeError()));
        continue;
      }
      printUnwindInfo(SW, Ctx, *Data, FunctionLength);
    }
  }
}

} // namespace llvm::RISCVWinEH
