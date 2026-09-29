// Copyright (c) 2026 Ahmed ARIF

// REQUIRES: powerpc-registered-target
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O2 -S -o - %s | FileCheck %s
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O2 -c -o %t.obj %s

extern void may_fault(void);

void funclet_spills(volatile int *p) {
  __try {
    may_fault();
  } __finally {
    __asm__ volatile ("" ::: "r29", "f14");
    *p = 42;
  }
}

// The parent saves its caller's nonvolatile registers in its own frame.
// Both integer and floating saves must be covered by the unwind prologue.
// The incoming 4(r1) is the caller's cross-image TOC restore slot. A parent
// with funclets must not overwrite it before allocating its own frame.
// CHECK-LABEL: ..funclet_spills:
// CHECK: mflr 0
// CHECK-NOT: stw 2, 4(1)
// CHECK: stwu 1,
// CHECK: mr 31, 1
// CHECK-DAG: stw 29, {{[0-9]+}}(31)
// CHECK-DAG: stfd 14, {{[0-9]+}}(31)
// CHECK: .seh_endprologue
// CHECK: bl ..may_fault

// In the funclet, r31 addresses the parent's locals while r1 addresses the
// funclet's own frame. Saving registers through r31 would overwrite the
// parent's saved caller registers, even when the finalizer returns normally.
// CHECK-LABEL: {{^"\?dtor\$.*funclet_spills.*":$}}
// CHECK: addi 31, 2, -{{[0-9]+}}
// CHECK: lis 2, .toc@ha
// CHECK: addi 2, 2, .toc@l
// CHECK-DAG: stw 29, [[GPR:[0-9]+]](1)
// CHECK-DAG: stfd 14, [[FPR:[0-9]+]](1)
// CHECK: .seh_endprologue
// CHECK: li {{[0-9]+}}, 42
// CHECK-DAG: lwz 29, [[GPR]](1)
// CHECK-DAG: lfd 14, [[FPR]](1)
// CHECK: blr

void funclet_aligned_spills(volatile int *p) {
  __declspec(align(64)) volatile int aligned;
  aligned = 0;
  __try {
    may_fault();
  } __finally {
    __asm__ volatile ("" ::: "r29", "f14");
    *p = ++aligned;
  }
}

// A realigned parent has no constant offset from its incoming SP. Reapply
// the alignment when locating its locals. The funclet's fixed callee saves
// must use its own incoming-SP base pointer, not its aligned SP or parent FP.
// CHECK-LABEL: ..funclet_aligned_spills:
// CHECK: clrlwi 0, 1, 26
// CHECK: mr 31, 1
// CHECK-DAG: stw 29, [[GPROFF:-[0-9]+]](30)
// CHECK-DAG: stfd 14, [[FPROFF:-[0-9]+]](30)
// CHECK: .seh_endprologue
// CHECK-LABEL: {{^"\?dtor\$.*funclet_aligned_spills.*":$}}
// CHECK: rlwinm 31, 2, 0, 0, 25
// CHECK: lis 2, .toc@ha
// CHECK: addi 31, 31, -{{[0-9]+}}
// CHECK: addi 2, 2, .toc@l
// CHECK-DAG: stw 29, [[GPROFF]](30)
// CHECK-DAG: stfd 14, [[FPROFF]](30)
// CHECK: .seh_endprologue
// CHECK-DAG: lwz 29, [[GPROFF]](30)
// CHECK-DAG: lfd 14, [[FPROFF]](30)
// CHECK: blr
