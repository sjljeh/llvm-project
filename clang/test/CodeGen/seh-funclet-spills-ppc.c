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
// Both integer and floating saves must be covered by the unwind prologue,
// stored below the incoming r1 before the frame is allocated (NT reverses
// only r1-relative stores). The incoming 4(r1) is the caller's cross-image
// TOC restore slot. A parent with funclets must not overwrite it.
// CHECK-LABEL: ..funclet_spills:
// CHECK: mflr 0
// CHECK-NOT: stw 2, 4(1)
// CHECK-DAG: stw 29, -{{[0-9]+}}(1)
// CHECK-DAG: stfd 14, -{{[0-9]+}}(1)
// CHECK: stwu 1,
// CHECK-NEXT: mr 31, 1
// CHECK: .seh_endprologue
// CHECK: bl ..may_fault

// In the funclet, r31 addresses the parent's locals while r1 addresses the
// funclet's own frame. Saving registers through r31 would overwrite the
// parent's saved caller registers, even when the finalizer returns normally.
// The funclet is an ordinary NT frame: it saves below its own incoming r1
// before allocating, and only then derives r31 and its TOC.
// CHECK-LABEL: {{^"\?dtor\$.*funclet_spills.*":$}}
// CHECK-DAG: stw 31, -148(1)
// CHECK-DAG: stw 29, -156(1)
// CHECK-DAG: stfd 14, -144(1)
// CHECK: stwu 1, -224(1)
// CHECK-NEXT: addi 31, 2, -224
// CHECK-NEXT: lis 2, .toc@ha
// CHECK-NEXT: addi 2, 2, .toc@l
// CHECK-NEXT: .seh_endprologue
// CHECK: li {{[0-9]+}}, 42
// CHECK-DAG: lwz 29, 68(1)
// CHECK-DAG: lfd 14, 80(1)
// CHECK-DAG: lwz 31, 76(1)
// CHECK: addi 1, 1, 224
// CHECK-NEXT: blr

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
// the alignment when locating its locals. The funclet's callee saves go below
// its own incoming SP and are restored from its back chain; its base pointer
// is the parent's incoming SP (r2), which locates the parent's fixed objects.
// CHECK-LABEL: ..funclet_aligned_spills:
// CHECK-DAG: clrlwi 12, 1, 26
// CHECK-DAG: stw 29, [[GPROFF:-[0-9]+]](1)
// CHECK-DAG: stfd 14, [[FPROFF:-[0-9]+]](1)
// CHECK-DAG: mr 30, 1
// CHECK: stwux 1, 1, 12
// CHECK-NEXT: mr 31, 1
// CHECK: .seh_endprologue
// CHECK-LABEL: {{^"\?dtor\$.*funclet_aligned_spills.*":$}}
// CHECK-NOT: stwx
// CHECK-DAG: clrlwi 12, 1, 26
// CHECK-DAG: stw 29, [[GPROFF]](1)
// CHECK-DAG: stfd 14, [[FPROFF]](1)
// CHECK-DAG: addi 30, 2, 0
// CHECK: stwux 1, 1, 12
// CHECK-DAG: rlwinm 31, 2, 0, 0, 25
// CHECK-DAG: lis 2, .toc@ha
// CHECK-DAG: addi 31, 31, -{{[0-9]+}}
// CHECK-DAG: addi 2, 2, .toc@l
// CHECK: .seh_endprologue
// CHECK: lwz 12, 0(1)
// CHECK-DAG: lwz 29, [[GPROFF]](12)
// CHECK-DAG: lfd 14, [[FPROFF]](12)
// CHECK: mr 1, 12
// CHECK-NEXT: blr
