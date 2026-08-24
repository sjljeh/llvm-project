// REQUIRES: mips-registered-target

// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --hex-dump=.pdata %t.obj \
// RUN:   | FileCheck %s --check-prefix=OBJ \
// RUN:   --implicit-check-not=IMAGE_REL_MIPS_REFWORDNB

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
// ASM: "?dtor$2@?0??cleanup@@YAXXZ@4HA":
// ASM-NEXT: .seh_proc "?dtor$2@?0??cleanup@@YAXXZ@4HA"
// ASM-NOT: .seh_handler
// ASM: sw $2, 28($sp)
// ASM-NEXT: move $fp, $2
// ASM-NEXT: .seh_endprologue
// ASM: addiu $4, $fp, -13
// ASM: jr $ra

// OBJ: Format: COFF-MIPS
// OBJ: Name: .pdata
// OBJ: RawDataSize: 40
// OBJ: Section {{.*}} .pdata {
// OBJ: 0x0 IMAGE_REL_MIPS_REFWORD .text
// OBJ: 0x4 IMAGE_REL_MIPS_REFWORD .text
// OBJ: 0x8 IMAGE_REL_MIPS_REFWORD __CxxFrameHandler
// OBJ: 0xC IMAGE_REL_MIPS_REFWORD $cppxdata$?cleanup@@YAXXZ
// OBJ: 0x10 IMAGE_REL_MIPS_REFWORD .text
// OBJ: 0x14 IMAGE_REL_MIPS_REFWORD .text
// OBJ-NEXT: 0x18 IMAGE_REL_MIPS_REFWORD .text
// OBJ-NEXT: 0x24 IMAGE_REL_MIPS_REFWORD .text
// OBJ: Hex dump of section '.pdata':
// OBJ: 0x00000020 00000000 58000000
