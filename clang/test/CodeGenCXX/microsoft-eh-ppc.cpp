// REQUIRES: powerpc-registered-target
//
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR \
// RUN:   --implicit-check-not=__CxxFrameHandler3
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --hex-dump=.xdata \
// RUN:   --hex-dump=.pdata %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -o %t.s %s
// RUN: %clangxx -target powerpcle-pc-windows-msvc -c -o %t.roundtrip.obj %t.s
// RUN: llvm-readobj --relocations --hex-dump=.pdata %t.roundtrip.obj \
// RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

extern void f();
extern void use(int);

void g() {
  try {
    f();
  } catch (int value) {
    use(value);
  }
}

// IR-LABEL: define dso_local void @"?g@@YAXXZ"()
// IR-SAME: personality ptr @__CxxFrameHandler
// IR: declare dso_local i32 @__CxxFrameHandler(...)

// The parent saves its TOC where the VC4 runtime expects it. The catch funclet
// receives the parent's incoming SP in r2, derives the parent frame pointer,
// restores the real TOC, and returns the continuation address in r3.
// ASM: .seh_handler __CxxFrameHandler, @unwind, @except
// ASM: stw 2, 8(1)
// ASM: stwu 1, -64(1)
// ASM: .seh_endprologue
// ASM: "$cppxdata$?g@@YAXXZ":
// ASM-NEXT: .long 429065504{{.*}}# MagicNumber
// ASM: "$handlerMap$0$?g@@YAXXZ":
// ASM-NEXT: .long 0{{.*}}# Adjectives
// ASM-NEXT: .long "??_R0H@8"{{.*}}# Type
// ASM-NEXT: .long -4{{.*}}# CatchObjOffset
// ASM-NEXT: .long "?catch$4@?0??g@@YAXXZ@4HA"{{.*}}# Handler
// ASM-NEXT: "$ip2state$?g@@YAXXZ":
// ASM: "?catch$4@?0??g@@YAXXZ@4HA":
// ASM-NEXT: .seh_proc "?catch$4@?0??g@@YAXXZ@4HA"
// ASM-NOT: .seh_handler
// ASM: stwu 1, -64(1)
// ASM: addi 31, 2, -64
// ASM-NEXT: lwz 2, 8(2)
// ASM-NEXT: .seh_endprologue
// ASM: lwz 3, 60(31)
// ASM: lis 3, .LBB0_1@ha
// ASM-NEXT: addi 3, 3, .LBB0_1@l
// ASM-NEXT: blr

// The object contains one five-word parent RUNTIME_FUNCTION and one five-word
// catch-funclet record. Only the parent record has a handler and HandlerData.
// OBJ: Format: COFF-PowerPC
// OBJ: Name: .xdata
// OBJ: RawDataSize: 112
// OBJ: Name: .pdata
// OBJ: RawDataSize: 40
// OBJ: RelocationCount: 8
// OBJ: Section {{.*}} .xdata {
// OBJ: IMAGE_REL_PPC_ADDR32 $stateUnwindMap$?g@@YAXXZ
// OBJ: IMAGE_REL_PPC_ADDR32 $tryMap$?g@@YAXXZ
// OBJ: IMAGE_REL_PPC_ADDR32 $ip2state$?g@@YAXXZ
// OBJ: IMAGE_REL_PPC_ADDR32 $handlerMap$0$?g@@YAXXZ
// OBJ: IMAGE_REL_PPC_ADDR32 ??_R0H@8
// OBJ: IMAGE_REL_PPC_ADDR32 ?catch$4@?0??g@@YAXXZ@4HA
// OBJ: Section {{.*}} .pdata {
// OBJ: 0x0 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x4 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x8 IMAGE_REL_PPC_ADDR32 __CxxFrameHandler
// OBJ-NEXT: 0xC IMAGE_REL_PPC_ADDR32 $cppxdata$?g@@YAXXZ
// OBJ-NEXT: 0x10 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x14 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x18 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x24 IMAGE_REL_PPC_ADDR32 .text
// OBJ: Hex dump of section '.xdata':
// OBJ-NEXT: 0x00000000 20059319 02000000 00000000 01000000
// OBJ: 0x00000040 00000000 00000000 fcffffff 00000000
// OBJ: Hex dump of section '.pdata':
// OBJ-NEXT: 0x00000000 00000000 3c000000 00000000 00000000
// OBJ-NEXT: 0x00000010 18000000 3c000000 78000000 00000000
// OBJ-NEXT: 0x00000020 00000000 54000000

// ROUNDTRIP: Section {{.*}} .pdata {
// ROUNDTRIP: 0x8 IMAGE_REL_PPC_ADDR32 __CxxFrameHandler
// ROUNDTRIP-NEXT: 0xC IMAGE_REL_PPC_ADDR32 .xdata
// ROUNDTRIP: Hex dump of section '.pdata':
// ROUNDTRIP-NEXT: 0x00000000 00000000 3c000000 00000000 00000000
// ROUNDTRIP-NEXT: 0x00000010 18000000 3c000000 78000000 00000000
// ROUNDTRIP-NEXT: 0x00000020 00000000 54000000
