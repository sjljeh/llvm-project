; NTUW unwind rules for PowerPC frames.
; RUN: llc -mtriple=powerpcle-pc-windows-msvc19.33 < %s | FileCheck %s
; RUN: llc -mtriple=powerpcle-pc-windows-msvc19.33 -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=powerpcle-pc-windows-msvc12.0 < %s \
; RUN:   | FileCheck %s --check-prefix=LEGACY --implicit-check-not=.seh_set_cfa

declare void @g(ptr)

define void @simple() {
  %a = alloca [4 x i32]
  call void @g(ptr %a)
  ret void
}

define void @big() {
  %a = alloca [40000 x i8]
  call void @g(ptr %a)
  ret void
}

define void @dyn(i32 %n) {
  %a = alloca i8, i32 %n
  call void @g(ptr %a)
  ret void
}

; LR is saved before the frame is allocated, as NT 4.0 requires.
; CHECK-LABEL: ..simple:
; CHECK:       mflr 0
; CHECK-NEXT:  stw 0, 8(1)
; CHECK-NEXT:  .seh_save_spr lr, 8
; CHECK-NEXT:  stwu 1, -80(1)
; CHECK-NEXT:  .seh_set_cfa r1, 80
; CHECK:       .seh_endprologue
; CHECK:       lwz 0, 88(1)
; CHECK-NEXT:  .seh_startepilogue
; CHECK-NEXT:  mtlr 0
; CHECK-NEXT:  .seh_same_spr lr
; CHECK-NEXT:  addi 1, 1, 80
; CHECK-NEXT:  .seh_set_cfa r1, 0
; CHECK-NEXT:  blr
; CHECK-NEXT:  .seh_endepilogue

; CHECK-LABEL: ..big:
; CHECK:       stwux 1, 1, 12
; CHECK-NEXT:  .seh_set_cfa r1, 40064

; The epilogue takes the CFA from the back chain into r12.
; CHECK-LABEL: ..dyn:
; CHECK:       mr 31, 1
; CHECK-NEXT:  .seh_set_cfa r31, 80
; CHECK:       .seh_endprologue
; CHECK:       lwz 12, 0(1)
; CHECK-NEXT:  lwz 0, 8(12)
; CHECK-NEXT:  .seh_startepilogue
; CHECK-NEXT:  lwz 31, -4(12)
; CHECK-NEXT:  .seh_set_cfa r12, 0
; CHECK-NEXT:  .seh_same_gpr r31

; OBJ:      UnwindFormat: NTUW
; OBJ-COUNT-3: Magic: 0x5755544E
; OBJ-NOT:  ValidationError

; LEGACY-LABEL: ..simple:
; LEGACY:       .seh_endprologue
