// Copyright (c) 2026 Ahmed ARIF

// REQUIRES: powerpc-registered-target, x86-registered-target
//
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang -target x86_64-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O2 -S -o - %s | FileCheck %s --check-prefix=ASM

extern void never_returns(void) __attribute__((noreturn));

int nested_cleanup(volatile int *fault) {
  volatile int flags = 0;
  __try {
    __try {
      __try {
        *fault = 1;
      } __finally {
        never_returns();
      }
    } __finally {
      flags |= 8;
    }
  } __except (1) {
    flags |= 16;
  }
  return flags;
}

// Even though the inner finalizer does not return normally, the faulting
// store is protected by both finalizers and the outer exception handler. The
// state numbering is target independent; x86-64 is affected the same way.
// ASM: .seh_handlerdata
// ASM: .long [[BEGIN:.Ltmp[0-9]+]]{{(@IMGREL)?}} # LabelStart
// ASM-NEXT: .long [[END:.Ltmp[0-9]+]]{{(@IMGREL)?}} # LabelEnd
// ASM: # FinallyFunclet
// ASM: .long [[BEGIN]]{{(@IMGREL)?}} # LabelStart
// ASM-NEXT: .long [[END]]{{(@IMGREL)?}} # LabelEnd
// ASM: # FinallyFunclet
// ASM: .long [[BEGIN]]{{(@IMGREL)?}} # LabelStart
// ASM-NEXT: .long [[END]]{{(@IMGREL)?}} # LabelEnd
// ASM: # CatchAll
