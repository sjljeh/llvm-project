; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O2 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O0 -verify-machineinstrs < %s | FileCheck %s

; The NT PowerPC unwinder (RtlVirtualUnwind) has no unwind codes. At any PC
; other than a blr it reverse-executes the prologue,
; honouring only mflr/mfcr, stw/stfd relative to r1, and stwu/stwux r1 (which
; reload r1 from the back chain). At a blr it takes the context as is. So:
;  * LR, CR, r31 and the callee saves are stored at negative offsets from the
;    incoming r1 before the frame is allocated (as Windows' own code does),
;    so that
;    they are found relative to the CFA even after alloca moves r1;
;  * the frame is allocated by a single stwu/stwux r1, after probing the pages
;    through r12 without moving r1;
;  * the epilogue keeps r1 at its body value and releases the frame with the
;    instruction directly before the blr.

declare void @g(i32)
declare void @gp(ptr)

; CHECK-LABEL: ..call:
; CHECK:      mflr 0
; CHECK-NEXT: stw 0, 8(1)
; CHECK:      stwu 1, -64(1)
; CHECK:      .seh_endprologue
; CHECK:      lwz 0, 72(1)
; CHECK:      mtlr 0
; CHECK:      addi 1, 1, 64
; CHECK-NEXT: blr
define i32 @call(i32 %a) {
  call void @g(i32 %a)
  ret i32 %a
}

; GPR, FPR and CR saves, all below the incoming r1. The CR is restored whole
; (mtcrf 255), as NT's restore millicode does.
; CHECK-LABEL: ..saves:
; CHECK-DAG:  mflr 0
; CHECK-DAG:  stw 0, 8(1)
; CHECK-DAG:  mfcr 12
; CHECK-DAG:  stw 12, -160(1)
; CHECK-DAG:  stw 29, -156(1)
; CHECK-DAG:  stfd 14, -144(1)
; CHECK:      stwu 1, -224(1)
; CHECK-NEXT: .seh_endprologue
; CHECK-DAG:  lwz 0, 232(1)
; CHECK-DAG:  lwz 29, 68(1)
; CHECK-DAG:  lfd 14, 80(1)
; CHECK-DAG:  mtcr 0
; CHECK:      addi 1, 1, 224
; CHECK-NEXT: blr
define void @saves(i32 %a) {
  call void asm sideeffect "", "~{r29},~{f14},~{cr2}"()
  call void @g(i32 %a)
  ret void
}

; A probed frame touches each page below r1 through r12 and then allocates
; with one stwu.
; CHECK-LABEL: ..probed_small:
; CHECK-DAG:  stw 0, 8(1)
; CHECK-DAG:  mr 12, 1
; CHECK:      stwu 0, -4096(12)
; CHECK-NOT:  1, -4096
; CHECK:      stwu 1, -6064(1)
; CHECK:      .seh_endprologue
; CHECK:      addi 1, 1, 6064
; CHECK-NEXT: blr
define void @probed_small() "probe-stack"="inline-asm" {
  %buf = alloca [6000 x i8]
  call void @gp(ptr %buf)
  ret void
}

; Many pages: a CTR loop through r12, then lis/ori r12 and stwux. The
; epilogue restores relative to the CFA from the back chain.
; CHECK-LABEL: ..probed_loop:
; CHECK-DAG:  stw 0, 8(1)
; CHECK-DAG:  stw 30, -8(1)
; CHECK-DAG:  mr 12, 1
; CHECK-DAG:  li 0, 9
; CHECK:      mtctr 0
; CHECK-NEXT: .LBB{{.*}}:
; CHECK-NEXT: stwu 0, -4096(12)
; CHECK-NEXT: bdnz
; CHECK-NEXT: %bb.
; CHECK-NEXT: lis 12, -1
; CHECK-NEXT: ori 12, 12, 25472
; CHECK-NEXT: stwux 1, 1, 12
; CHECK:      .seh_endprologue
; CHECK:      lwz 12, 0(1)
; CHECK-DAG:  lwz 0, 8(12)
; CHECK-DAG:  lwz 30, -8(12)
; CHECK:      mr 1, 12
; CHECK-NEXT: blr
define void @probed_loop() "probe-stack"="inline-asm" {
  %buf = alloca [40000 x i8]
  call void asm sideeffect "", "~{r30}"()
  call void @gp(ptr %buf)
  ret void
}

; A large frame without probing.
; CHECK-LABEL: ..large:
; CHECK-NOT:  stwx
; CHECK-DAG:  stw 0, 8(1)
; CHECK-DAG:  lis 12, -1
; CHECK-DAG:  ori 12, 12, 25472
; CHECK:      stwux 1, 1, 12
; CHECK:      .seh_endprologue
; CHECK:      lwz 12, 0(1)
; CHECK:      lwz 0, 8(12)
; CHECK:      mr 1, 12
; CHECK-NEXT: blr
define void @large() {
  %buf = alloca [40000 x i8]
  call void @gp(ptr %buf)
  ret void
}

; With alloca, r31 and the callee saves are stored relative to the incoming
; r1, not through r31, and the dynamic allocation keeps the CFA in the back
; chain, which the epilogue reloads.
; CHECK-LABEL: ..dynamic:
; CHECK-DAG:  stw 0, 8(1)
; CHECK-DAG:  stw 31, -4(1)
; CHECK-DAG:  stw 30, -8(1)
; CHECK:      stwu 1, -[[SIZE:[0-9]+]](1)
; CHECK-NEXT: mr 31, 1
; CHECK:      addi [[CFA:[0-9]+]], 31, [[SIZE]]
; CHECK:      stwux [[CFA]], 1,
; CHECK:      lwz 12, 0(1)
; CHECK-DAG:  lwz 0, 8(12)
; CHECK-DAG:  lwz 30, -8(12)
; CHECK-DAG:  lwz 31, -4(12)
; CHECK:      mr 1, 12
; CHECK-NEXT: blr
define void @dynamic(i32 %n) "probe-stack"="inline-asm" {
  %p = alloca i8, i32 %n
  call void asm sideeffect "", "~{r30}"()
  call void @gp(ptr %p)
  ret void
}

; Realignment allocates with stwux r1 and an aligned delta; the base pointer
; is the incoming r1.
; CHECK-LABEL: ..realigned:
; CHECK-DAG:  stw 0, 8(1)
; CHECK-DAG:  stw 30, -8(1)
; CHECK-DAG:  mr 30, 1
; CHECK-DAG:  clrlwi 12, 1, 26
; CHECK-DAG:  subfic 12, 12, -192
; CHECK:      stwux 1, 1, 12
; CHECK:      .seh_endprologue
; CHECK:      lwz 12, 0(1)
; CHECK-DAG:  lwz 0, 8(12)
; CHECK-DAG:  lwz 30, -8(12)
; CHECK:      mr 1, 12
; CHECK-NEXT: blr
define void @realigned() {
  %buf = alloca [64 x i8], align 64
  call void @gp(ptr %buf)
  ret void
}
