// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -c %s -o %t.obj
// RUN: llvm-readobj --file-headers --relocations --symbols %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: lld-link /machine:alpha /entry:start /subsystem:native /nodefaultlib %t.obj /out:%t.exe
// RUN: llvm-readobj --file-headers %t.exe | FileCheck %s --check-prefix=EXE

__attribute__((noinline)) int add(int a, int b) { return a + b; }
int add_tail(void) { return add(3, 4); }
int start(void) { return add(1, 2) + add_tail(); }

// ASM-LABEL: add:
// ASM: addq $17,$16,$0
// A call in tail position releases the frame and branches (NTUW).
// ASM-LABEL: add_tail:
// ASM: lda $16,3($31)
// ASM: lda $17,4($31)
// ASM: br $31,add
// ASM-LABEL: start:
// ASM: lda $16,1($31)
// ASM: lda $17,2($31)
// ASM: bsr $26,$add..ng
// ASM: ret

// OBJ: Format: COFF-Alpha
// OBJ: Name: add
// OBJ: Name: start

// EXE: Format: COFF-Alpha
// EXE: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
// EXE: Subsystem: IMAGE_SUBSYSTEM_NATIVE (0x1)
