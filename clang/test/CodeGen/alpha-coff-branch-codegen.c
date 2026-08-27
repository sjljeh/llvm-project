// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -c %s -o %t.obj
// RUN: llvm-readobj --file-headers --symbols %t.obj | FileCheck %s --check-prefix=OBJ

int test(int a) {
  if (a)
    return 1;
  return 2;
}

int cmp(int a, int b) {
  if (a == b)
    return 3;
  return 4;
}

// ASM-LABEL: test:
// ASM: zapnot $16,15,$2
// ASM: cmoveq $2,2,$0
// ASM: ret
// ASM-LABEL: cmp:
// ASM: cmpeq $2,$0,$2
// ASM: cmoveq $2,4,$0
// ASM: ret

// OBJ: Format: COFF-Alpha
// OBJ: AddressSize: 32bit
// OBJ: Name: test
// OBJ: Name: cmp
