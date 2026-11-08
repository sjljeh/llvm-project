; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O0 < %s | FileCheck %s

; NT recognizes a PowerPC epilogue only at the blr and reverse-executes the
; prologue at every earlier PC, so the frame is released by the instruction
; directly before the return, after LR is restored.

declare void @g(i32)

; CHECK-LABEL: ..call:
; CHECK:      mflr 0
; CHECK-NEXT: stwu 1, -64(1)
; CHECK-NEXT: stw 0, 72(1)
; CHECK:      lwz 0, 72(1)
; CHECK-NEXT: mtlr 0
; CHECK-NEXT: addi 1, 1, 64
; CHECK-NEXT: blr
define i32 @call(i32 %a) {
  call void @g(i32 %a)
  ret i32 %a
}
