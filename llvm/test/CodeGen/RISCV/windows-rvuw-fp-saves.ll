; RUN: llc -mtriple=riscv64-pc-windows-msvc -mattr=+d -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-pc-windows-msvc -mattr=+d -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
; RUN: not llc -mtriple=riscv64-pc-windows-msvc -mattr=+f -target-abi=lp64f < %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOD
;
; RVUW describes floating-point callee saves with SAVE_FPR/SAME_FPR, and gives
; every instruction that writes SP an exact CFA rule, including both halves of
; an adjustment that is split into two ADDIs.

declare void @g(ptr)

; CHECK-LABEL: fp_saves:
; CHECK:         addi sp, sp, -2032
; CHECK-NEXT:    .seh_set_cfa x2, 2032
; CHECK:         fsd fs0,
; CHECK-NEXT:    .seh_save_fpr f8, -16
; CHECK:         fsd fs1,
; CHECK-NEXT:    .seh_save_fpr f9, -24
; CHECK-NEXT:    addi sp, sp, -1008
; CHECK-NEXT:    .seh_set_cfa x2, 3040
; CHECK-NEXT:    .seh_endprologue
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, sp, 1008
; CHECK-NEXT:    .seh_set_cfa x2, 2032
; CHECK:         fld fs0,
; CHECK-NEXT:    .seh_same_fpr f8
; CHECK:         fld fs1,
; CHECK-NEXT:    .seh_same_fpr f9
; CHECK-NEXT:    addi sp, sp, 2032
; CHECK-NEXT:    .seh_set_cfa x2, 0
; CHECK-NEXT:    ret
; CHECK-NEXT:    .seh_endepilogue
define double @fp_saves(double %x, double %y) {
  %a = alloca [3000 x i8]
  call void @g(ptr %a)
  %s = fadd double %x, %y
  call void @g(ptr %a)
  %t = fmul double %s, %x
  ret double %t
}

; NOD: RISC-V64 RVUW requires the D extension to describe floating-point callee saves

; UNWIND:      RequiredState: 0x3
; UNWIND:      Opcode: SAVE_FPR
; UNWIND-NEXT: Register: f8 (8)
; UNWIND-NEXT: Operand: -16
; UNWIND:      Opcode: SAVE_FPR
; UNWIND-NEXT: Register: f9 (9)
; UNWIND-NEXT: Operand: -24
; UNWIND:      EpilogScopes [
; UNWIND:      Opcode: SAME_FPR
; UNWIND-NEXT: Register: f8 (8)

; A float callee save under F without D would be a 32-bit spill.
; CHECK-LABEL: float_save:
; CHECK:         .seh_save_fpr f8,
define float @float_save(float %x) {
  call void @g(ptr null)
  %r = fadd float %x, %x
  ret float %r
}

; A leaf frame of 2048 to 4095 bytes is allocated by two ADDIs; each one gets
; its own rule.
; CHECK-LABEL: split_adjust:
; CHECK:         addi sp, sp, -2048
; CHECK-NEXT:    .seh_set_cfa x2, 2048
; CHECK-NEXT:    addi sp, sp, -{{[0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x2, [[SIZE:[0-9]+]]
; CHECK-NEXT:    .seh_endprologue
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, sp, 2032
; CHECK-NEXT:    .seh_set_cfa x2, {{[0-9]+}}
; CHECK-NEXT:    addi sp, sp, {{[0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x2, 0
define void @split_adjust(i64 %i) {
  %a = alloca [3000 x i8]
  %p = getelementptr i8, ptr %a, i64 %i
  store volatile i8 0, ptr %p
  ret void
}
