; RUN: llc -mtriple=riscv64-w64-windows-gnu < %s | FileCheck %s

; A function whose body is only unreachable must still own an address range
; for its RVUW entry.

define void @f() {
; CHECK-LABEL: f:
; CHECK: unimp
  unreachable
}
