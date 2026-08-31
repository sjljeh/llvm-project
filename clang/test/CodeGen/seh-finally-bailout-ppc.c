// REQUIRES: powerpc-registered-target
//
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -Wno-jump-seh-finally -O0 -S -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=IR
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -Wno-jump-seh-finally -O0 -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -Wno-jump-seh-finally -O0 -c -o %t.obj %s
// RUN: llvm-nm --undefined-only %t.obj \
// RUN:   | FileCheck %s --check-prefix=NM

extern void may_fault(void);

int seh_finally_return(volatile int *p) {
  __try {
    may_fault();
  } __finally {
    if (*p)
      return 42;
  }
  return 0;
}

// Jumps out of a finally during unwind use the generic non-x86 local-unwind
// path. This requires the target runtime to provide _local_unwind.
// IR-LABEL: define dso_local i32 @seh_finally_return(
// IR: invoke void @llvm.seh.localunwind()
// IR: catchpad within {{.*}} [ptr @__IsLocalUnwind]
// IR: declare extern_weak void @__IsLocalUnwind(ptr, ptr)
// IR: declare void @llvm.seh.localunwind()

// ASM-LABEL: ..seh_finally_return:
// ASM: .seh_handler __C_specific_handler, @unwind, @except
// ASM: bl .._local_unwind

// NM: U .._local_unwind
// NM: U __C_specific_handler
