// RUN: %clang -target alpha-pc-windows-msvc -c %s -o %t.obj
// RUN: llvm-readobj --file-headers --relocations --hex-dump=.data %t.obj | FileCheck %s
// RUN: lld-link /machine:alpha64 /entry:start /subsystem:native /nodefaultlib %t.obj /out:%t.exe
// RUN: llvm-readobj --file-headers %t.exe | FileCheck %s --check-prefix=EXE

int x = 42;
int *p = &x;
int start(void) { return 0; }

// CHECK: Format: COFF-Alpha64
// CHECK: AddressSize: 64bit
// CHECK: Machine: IMAGE_FILE_MACHINE_ALPHA64 (0x284)
// CHECK: IMAGE_REL_ALPHA_REFQUAD x
// CHECK: 0x00000000 2a000000 00000000 00000000 00000000

// EXE: Format: COFF-Alpha64
// EXE: Machine: IMAGE_FILE_MACHINE_ALPHA64 (0x284)
// EXE: Magic: 0x20B
// EXE: ImageBase: 0x140000000
// EXE: Subsystem: IMAGE_SUBSYSTEM_NATIVE (0x1)
