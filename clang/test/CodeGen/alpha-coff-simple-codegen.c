// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -c %s -o %t.obj
// RUN: llvm-readobj --file-headers --symbols %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O1 -g -gcodeview -c %s -o %t.cv.obj
// RUN: llvm-readobj --codeview %t.cv.obj | FileCheck %s --check-prefix=CV

int start(void) { return 0; }
int id(int a) { return a; }
int add(int a, int b) { return a + b; }
long long add64(long long a, long long b) { return a + b; }
int add_imm(int a) { return a + 5; }

// ASM-LABEL: start:
// ASM: bis $31,$31,$0
// ASM: ret
// ASM-LABEL: id:
// ASM: bis $16,$16,$0
// ASM-LABEL: add:
// ASM: addq $17,$16,$0
// ASM-LABEL: add64:
// ASM: addq $17,$16,$0
// ASM-LABEL: add_imm:
// ASM: addq $16,5,$0

// OBJ: Format: COFF-Alpha
// OBJ: AddressSize: 32bit
// OBJ: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
// OBJ: Name: start
// OBJ: Name: id
// OBJ: Name: add
// OBJ: Name: add64
// OBJ: Name: add_imm

// CV: Machine: Alpha
// CV: VarName: a
// CV: Register: ALPHA_A0
