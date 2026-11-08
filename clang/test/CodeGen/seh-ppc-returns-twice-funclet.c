// Copyright (c) 2026 Ahmed ARIF

// REQUIRES: powerpc-registered-target
//
// RUN: %clang -target powerpcle-pc-windows-msvc12.0 -fms-extensions -fexceptions \
// RUN:   -O0 -S -o - %s | FileCheck %s --check-prefix=ASM

extern int setjmp(void *) __attribute__((returns_twice));
extern void touch(void);

int seh_with_setjmp(void *buffer) {
  int value = setjmp(buffer);
  __try {
    touch();
  } __finally {
    touch();
  }
  return value;
}

// The parent needs its frame pointer because setjmp returns twice. The
// generated cleanup funclet must still restore its own stack frame from r1;
// r31 addresses escaped parent locals and cannot be used as its stack base.
// ASM-LABEL: "?dtor$
// ASM: stw 31, -4(1)
// ASM-NEXT: stwu 1, -[[SIZE:[0-9]+]](1)
// ASM: addi 31, 2, -[[SIZE]]
// ASM: bl "..?fin$
// ASM-NOT: mr 1, 31
// NT recognizes the epilogue only at the blr, so the frame is released last,
// from the CFA in the funclet's own back chain.
// ASM: lwz 12, 0(1)
// ASM: lwz 31, -4(12)
// ASM-NEXT: mr 1, 12
// ASM-NEXT: blr
