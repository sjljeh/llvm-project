// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -c %s -o %t.obj
// RUN: llvm-readobj --file-headers --relocations --hex-dump=.data --hex-dump=.rdata %t.obj | FileCheck %s

const char s[] = "abc";
int x = 42;
int *p = &x;
struct S { int a; char b; } gs = {7, 'z'};
int start(void) { return 0; }

// CHECK: Format: COFF-Alpha
// CHECK: AddressSize: 32bit
// CHECK: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
// CHECK: IMAGE_REL_ALPHA_REFLONG x
// CHECK: Hex dump of section '.data':
// CHECK-NEXT: 0x00000000 2a000000 00000000 07000000 7a000000
// CHECK: Hex dump of section '.rdata':
// CHECK-NEXT: 0x00000000 61626300
