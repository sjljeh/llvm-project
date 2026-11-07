; RUN: llc -mtriple=riscv64-unknown-uefi-coff -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-unknown-uefi-coff -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
;
; Outside Windows, a COFF target may probe the stack inline. While the probe
; loop moves SP towards its fixed target register, the CFA is described
; through that (volatile) register; the loop makes no calls. The probing store
; that can fault therefore always has an exact rule, and the loop need not be
; unrolled.

declare void @g(ptr)

; CHECK-LABEL: big:
; CHECK:         sub t1, sp, {{[a-z0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x6, [[#LOOPCFA:]]
; CHECK:       .LBB0_1:
; CHECK-NEXT:    sub sp, sp, t2
; CHECK-NEXT:    sd zero, 0(sp)
; CHECK-NEXT:    bne sp, t1, .LBB0_1
; CHECK-NEXT:  # %bb.2:
; CHECK-NEXT:    .seh_set_cfa x2, [[#LOOPCFA]]
; CHECK-NEXT:    addi sp, sp, -2048
; CHECK-NEXT:    .seh_set_cfa x2, [[#LOOPCFA + 2048]]
; CHECK-NEXT:    addi sp, sp, -400
; CHECK-NEXT:    .seh_set_cfa x2, 70016
; CHECK-NEXT:    .seh_endprologue
define void @big() uwtable "probe-stack"="inline-asm" {
  %a = alloca [70000 x i8]
  call void @g(ptr %a)
  ret void
}

; UNWIND-NOT:  ValidationError
; UNWIND:      Opcode: SET_CFA
; UNWIND-NEXT: Register: x2 (2)
; UNWIND:      Opcode: SET_CFA
; UNWIND-NEXT: Register: x6 (6)
; UNWIND-NEXT: Operand: 67568
; UNWIND-NOT:  ValidationError
