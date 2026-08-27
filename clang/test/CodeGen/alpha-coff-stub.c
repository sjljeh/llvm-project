// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -c %s -o %t.obj
// RUN: llvm-readobj --file-headers --symbols %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -fuse-ld=lld -nostdlib -Wl,/entry:start -Wl,/subsystem:native %s -o %t.exe
// RUN: llvm-readobj --file-headers %t.exe | FileCheck %s --check-prefix=EXE

int start(void) { return 0; }

// ASM-LABEL: start:
// ASM: bis $31,$31,$0
// ASM: ret

// OBJ: Format: COFF-Alpha
// OBJ: Arch: alpha
// OBJ: AddressSize: 32bit
// OBJ: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
// OBJ: Name: start

// EXE: Format: COFF-Alpha
// EXE: Arch: alpha
// EXE: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
// EXE: IMAGE_FILE_EXECUTABLE_IMAGE
// EXE: Subsystem: IMAGE_SUBSYSTEM_NATIVE (0x1)
