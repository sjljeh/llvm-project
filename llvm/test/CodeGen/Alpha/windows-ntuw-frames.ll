; NTUW unwind rules for Alpha frames.
; RUN: llc -mtriple=alpha-pc-windows-msvc19.33 < %s | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc19.33 -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=alpha-pc-windows-msvc12.0 < %s \
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

; CHECK-LABEL: simple:
; CHECK:       lda $30,-32($30)
; CHECK-NEXT:  .seh_set_cfa 30, 32
; CHECK-NEXT:  stq $26,24($30)
; CHECK-NEXT:  .seh_save_gpr 26, -8
; CHECK-NEXT:  .seh_endprologue
; CHECK:       .seh_startepilogue
; CHECK-NEXT:  ldq $26,24($30)
; CHECK-NEXT:  .seh_same_gpr 26
; CHECK-NEXT:  lda $30,32($30)
; CHECK-NEXT:  .seh_set_cfa 30, 0
; CHECK-NEXT:  ret
; CHECK-NEXT:  .seh_endepilogue

; Each step of a two-instruction allocation changes the CFA rule.
; CHECK-LABEL: big:
; CHECK:       ldah $30,-1($30)
; CHECK-NEXT:  .seh_set_cfa 30, 65552
; CHECK-NEXT:  lda $30,25536($30)
; CHECK-NEXT:  .seh_set_cfa 30, 40016
; CHECK-NEXT:  .seh_endprologue
; CHECK:       addq $30,$28,$30
; CHECK-NEXT:  .seh_set_cfa 30, 0

; With alloca the CFA is $15-relative until $15 is reloaded.
; CHECK-LABEL: dyn:
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  .seh_set_cfa 15, 32
; CHECK-NEXT:  .seh_endprologue
; CHECK:       bis $15,$15,$30
; CHECK-NEXT:  .seh_startepilogue
; CHECK-NEXT:  ldq $26,24($30)
; CHECK-NEXT:  .seh_same_gpr 26
; CHECK-NEXT:  ldq $15,0($30)
; CHECK-NEXT:  .seh_set_cfa 30, 32
; CHECK-NEXT:  .seh_same_gpr 15

; OBJ:      UnwindFormat: NTUW
; OBJ-COUNT-3: Magic: 0x5755544E
; OBJ-NOT:  ValidationError

; LEGACY-LABEL: simple:
; LEGACY:       .seh_endprologue
