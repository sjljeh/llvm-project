// REQUIRES: mips-registered-target

// RUN: %clang -target mipsel-pc-windows-msvc -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
// RUN: %clang -target mipsel-pc-windows-msvc -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --hex-dump=.pdata %t.obj \
// RUN:   | FileCheck %s --check-prefix=OBJ \
// RUN:   --implicit-check-not=IMAGE_REL_MIPS_REFWORDNB

extern void f(void);

void leaf(void) {}

void caller(void) { f(); }

// ASM-LABEL: leaf:
// ASM-NEXT: .seh_proc leaf
// ASM: .seh_endprologue
// ASM: .seh_endproc
// ASM-LABEL: caller:
// ASM-NEXT: .seh_proc caller
// ASM: .seh_endprologue
// ASM: .seh_endproc

// OBJ: Format: COFF-MIPS
// OBJ: Name: .pdata
// OBJ: RawDataSize: 40
// OBJ: Section {{.*}} .pdata {
// OBJ: 0x0 IMAGE_REL_MIPS_REFWORD .text
// OBJ-NEXT: 0x4 IMAGE_REL_MIPS_REFWORD .text
// OBJ-NEXT: 0x10 IMAGE_REL_MIPS_REFWORD .text
// OBJ-NEXT: 0x14 IMAGE_REL_MIPS_REFWORD .text
// OBJ-NEXT: 0x18 IMAGE_REL_MIPS_REFWORD .text
// OBJ-NEXT: 0x24 IMAGE_REL_MIPS_REFWORD .text
// OBJ: Hex dump of section '.pdata':
// OBJ: 0x00000000 00000000 28000000 00000000 00000000
// OBJ: 0x00000010 10000000 28000000 58000000 00000000
// OBJ: 0x00000020 00000000 38000000
