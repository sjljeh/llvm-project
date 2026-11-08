// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fexceptions -fcxx-exceptions -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fexceptions -fcxx-exceptions -c %s -o %t.obj
// RUN: llvm-readobj --sections --relocations --symbols %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fexceptions -fcxx-exceptions -S %s -o %t.s
// RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %t.s -o %t.roundtrip.obj
// RUN: llvm-readobj --file-headers --sections --relocations %t.roundtrip.obj | FileCheck %s --check-prefix=ROUNDTRIP
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -fexceptions -fcxx-exceptions -S %s -o %t.o2.s
// RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %t.o2.s -o %t.o2.obj

extern void may_throw();

int alpha_try_catch() {
  try {
    may_throw();
    return 0;
  } catch (...) {
    return 1;
  }
}

// The NT Alpha C++ runtime (__CxxFrameHandler) reads the EH state from
// the { ULONG State; PVOID Rfp; } EH context record at EstablisherFrame +
// FuncInfo.EHContextDelta and calls funclets with Rfp in $1.

// ASM-LABEL: "?alpha_try_catch@@YAHXZ":
// ASM: .seh_proc "?alpha_try_catch@@YAHXZ"
// ASM: .seh_handler __CxxFrameHandler, @unwind, @except
// ASM: lda $30,-48($30)
// ASM-NEXT: stq $15,0($30)
// ASM-NEXT: stq $26,40($30)
// ASM-NEXT: bis $30,$30,$15
// ASM-NEXT: .seh_endprologue
// Rfp, then state -1 on entry and state 0 for the try.
// ASM-NEXT: stl $15,36($15)
// ASM-NEXT: lda $0,-1($31)
// ASM-NEXT: zapnot $0,15,$0
// ASM-NEXT: lda $1,32($15)
// ASM-NEXT: stl $0,0($1)
// ASM-NEXT: bis $31,$31,$0
// ASM-NEXT: stl $0,0($1)
// ASM: bsr $26,$"?may_throw@@YAXXZ"..ng
// ASM: lda $1,28($15)
// ASM: $ehgcr_0_3:
// ASM: .seh_handlerdata
// ASM: "$cppxdata$?alpha_try_catch@@YAHXZ":
// ASM-NEXT: .long 429065504 # MagicNumber
// ASM-NEXT: .long 2 # MaxState
// ASM-NEXT: .long "$stateUnwindMap$?alpha_try_catch@@YAHXZ" # UnwindMap
// ASM-NEXT: .long 1 # NumTryBlocks
// ASM-NEXT: .long "$tryMap$?alpha_try_catch@@YAHXZ" # TryBlockMap
// ASM-NEXT: .long -16 # EHContextDelta
// ASM-NEXT: .long 0 # IPMapEntries
// ASM-NEXT: .long 0 # IPToStateXData
// ASM: "$tryMap$?alpha_try_catch@@YAHXZ":
// ASM-NEXT: .long 0 # TryLow
// ASM-NEXT: .long 0 # TryHigh
// ASM-NEXT: .long 1 # NumCatches
// ASM-NEXT: .long "$handlerMap$0$?alpha_try_catch@@YAHXZ" # HandlerArray
// ASM: .long "?catch$1@?0??alpha_try_catch@@YAHXZ@4HA" # Handler
// ASM: .seh_endproc
// The catch has no handler of its own. It addresses the parent's frame
// through $15 = Rfp, and loads the continuation before releasing its frame.
// ASM: "?catch$1@?0??alpha_try_catch@@YAHXZ@4HA":
// ASM-NEXT: .seh_proc "?catch$1@?0??alpha_try_catch@@YAHXZ@4HA"
// ASM-NOT: .seh_handler
// ASM: lda $30,-48($30)
// ASM-NEXT: stq $15,0($30)
// ASM-NEXT: stq $26,40($30)
// ASM-NEXT: bis $1,$1,$15
// ASM-NEXT: .seh_endprologue
// ASM-NEXT: lda $1,28($15)
// ASM-NEXT: lda $0,1($31)
// ASM-NEXT: stl $0,0($1)
// ASM-NEXT: ldq $26,40($30)
// ASM-NEXT: ldah $0,$ehgcr_0_3($31)
// ASM-NEXT: lda $0,$ehgcr_0_3($0)
// ASM-NEXT: ldq $15,0($30)
// ASM-NEXT: lda $30,48($30)
// ASM-NEXT: ret
// ASM: .seh_endproc

// OBJ: Name: .xdata
// OBJ: RawDataSize: 0x50
// OBJ: Name: .pdata
// OBJ: RawDataSize: 0x28
// OBJ: IMAGE_REL_ALPHA_REFHI $ehgcr_0_3
// OBJ-NEXT: IMAGE_REL_ALPHA_PAIR
// OBJ-NEXT: IMAGE_REL_ALPHA_REFLO $ehgcr_0_3
// OBJ: Section (4) .xdata
// OBJ: IMAGE_REL_ALPHA_REFLONG ?catch$1@?0??alpha_try_catch@@YAHXZ@4HA
// Only the parent's .pdata record has a handler; .pdata points straight at
// FuncInfo. The catch funclet's record (at 0x14) leaves both words zero.
// OBJ: Section (6) .pdata {
// OBJ-NEXT: 0x0 IMAGE_REL_ALPHA_REFLONG .text
// OBJ-NEXT: 0x4 IMAGE_REL_ALPHA_REFLONG .text
// OBJ-NEXT: 0x8 IMAGE_REL_ALPHA_REFLONG __CxxFrameHandler
// OBJ-NEXT: 0xC IMAGE_REL_ALPHA_REFLONG $cppxdata$?alpha_try_catch@@YAHXZ
// OBJ-NEXT: 0x10 IMAGE_REL_ALPHA_REFLONG .text
// OBJ-NEXT: 0x14 IMAGE_REL_ALPHA_REFLONG .text
// OBJ-NEXT: 0x18 IMAGE_REL_ALPHA_REFLONG .text
// OBJ-NEXT: 0x24 IMAGE_REL_ALPHA_REFLONG .text
// OBJ-NEXT: }
// OBJ: Name: $ehgcr_0_3
// OBJ: Section: .text (1)
// OBJ: Name: ?catch$1@?0??alpha_try_catch@@YAHXZ@4HA
// OBJ: Section: .text (1)

// ROUNDTRIP: Format: COFF-Alpha
// ROUNDTRIP: AddressSize: 32bit
// ROUNDTRIP: Name: .xdata
// ROUNDTRIP: Name: .pdata
// ROUNDTRIP: IMAGE_REL_ALPHA_BRADDR ?may_throw@@YAXXZ
// ROUNDTRIP: IMAGE_REL_ALPHA_REFHI $ehgcr_0_3
// ROUNDTRIP-NEXT: IMAGE_REL_ALPHA_PAIR
// ROUNDTRIP-NEXT: IMAGE_REL_ALPHA_REFLO $ehgcr_0_3
// ROUNDTRIP: Section (4) .xdata
// ROUNDTRIP: Section (6) .pdata
// ROUNDTRIP: IMAGE_REL_ALPHA_REFLONG __CxxFrameHandler
