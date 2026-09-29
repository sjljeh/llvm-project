// Copyright (c) 2026 Ahmed ARIF

// REQUIRES: powerpc-registered-target
//
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
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
// ASM: stwu 1, -{{[0-9]+}}(1)
// ASM: addi 31, 2, -{{[0-9]+}}
// ASM: bl "..?fin$
// ASM: lwz 31, {{[0-9]+}}(1)
// ASM: addi 1, 1, {{[0-9]+}}
// ASM: blr
