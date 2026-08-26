// REQUIRES: powerpc-registered-target
//
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --hex-dump=.pdata %t.obj \
// RUN:   | FileCheck %s --check-prefix=OBJ

extern void f();

struct S {
  ~S();
};

void cleanup() {
  S value;
  f();
}

// ASM: .seh_handler __CxxFrameHandler, @unwind, @except
// ASM: "$cppxdata$?cleanup@@YAXXZ":
// ASM: .long "?dtor$2@?0??cleanup@@YAXXZ@4HA"{{.*}}# Action
// ASM: "?dtor$2@?0??cleanup@@YAXXZ@4HA":
// ASM-NEXT: .seh_proc "?dtor$2@?0??cleanup@@YAXXZ@4HA"
// ASM-NOT: .seh_handler
// ASM: stwu 1, -64(1)
// ASM: addi 31, 2, -64
// ASM-NEXT: lwz 2, 8(2)
// ASM-NEXT: .seh_endprologue
// ASM: addi 3, 31, 59
// ASM: blr

// OBJ: Format: COFF-PowerPC
// OBJ: Name: .pdata
// OBJ: RawDataSize: 40
// OBJ: RelocationCount: 8
// OBJ: Section {{.*}} .pdata {
// OBJ: 0x0 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x4 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x8 IMAGE_REL_PPC_ADDR32 __CxxFrameHandler
// OBJ-NEXT: 0xC IMAGE_REL_PPC_ADDR32 $cppxdata$?cleanup@@YAXXZ
// OBJ-NEXT: 0x10 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x14 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x18 IMAGE_REL_PPC_ADDR32 .text
// OBJ-NEXT: 0x24 IMAGE_REL_PPC_ADDR32 .text
// OBJ: Hex dump of section '.pdata':
// OBJ-NEXT: 0x00000000 00000000 3c000000 00000000 00000000
// OBJ-NEXT: 0x00000010 18000000 3c000000 70000000 00000000
// OBJ-NEXT: 0x00000020 00000000 54000000
