; RUN: llc -mtriple=riscv64-pc-windows-msvc -mattr=+zcmp -verify-machineinstrs < %s | FileCheck %s --check-prefix=PUSH
; RUN: llc -mtriple=riscv64-pc-windows-msvc -mattr=+save-restore -verify-machineinstrs < %s | FileCheck %s --check-prefix=LIBCALL
; RUN: llc -mtriple=riscv64-pc-windows-msvc -mattr=+zcmp -filetype=obj < %s -o %t.push.obj
; RUN: llvm-readobj --unwind %t.push.obj | FileCheck %s --check-prefix=VALID
; RUN: llc -mtriple=riscv64-pc-windows-msvc -mattr=+save-restore -filetype=obj < %s -o %t.libcall.obj
; RUN: llvm-readobj --unwind %t.libcall.obj | FileCheck %s --check-prefix=VALID
;
; Zcmp cm.push/cm.pop and the __riscv_save/__riscv_restore libcalls allocate
; and save (or restore and release) in one step; the RVUW rules after that
; step describe all of its effects.

declare void @g(ptr)

; PUSH-LABEL: saves:
; PUSH:         cm.push {ra, s0-s3}, -80
; PUSH-NEXT:    .seh_set_cfa x2, 80
; PUSH-NEXT:    .seh_save_gpr x1, -40
; PUSH-NEXT:    .seh_save_gpr x8, -32
; PUSH-NEXT:    .seh_save_gpr x9, -24
; PUSH-NEXT:    .seh_save_gpr x18, -16
; PUSH-NEXT:    .seh_save_gpr x19, -8
; PUSH-NEXT:    .seh_endprologue
; PUSH:         .seh_startepilogue
; PUSH-NEXT:    cm.pop {ra, s0-s3}, 80
; PUSH-NEXT:    .seh_same_gpr x1
; PUSH-NEXT:    .seh_same_gpr x8
; PUSH-NEXT:    .seh_same_gpr x9
; PUSH-NEXT:    .seh_same_gpr x18
; PUSH-NEXT:    .seh_same_gpr x19
; PUSH-NEXT:    .seh_set_cfa x2, 0
; PUSH-NEXT:    ret
; PUSH-NEXT:    .seh_endepilogue

; LIBCALL-LABEL: saves:
; LIBCALL:         call t0, __riscv_save_4
; LIBCALL-NEXT:    .seh_set_cfa x2, 48
; LIBCALL-NEXT:    .seh_save_gpr x1, -8
; LIBCALL-NEXT:    .seh_save_gpr x8, -16
; LIBCALL-NEXT:    .seh_save_gpr x9, -24
; LIBCALL-NEXT:    .seh_save_gpr x18, -32
; LIBCALL-NEXT:    .seh_save_gpr x19, -40
; LIBCALL-NEXT:    addi sp, sp, -32
; LIBCALL-NEXT:    .seh_set_cfa x2, 80
; LIBCALL-NEXT:    .seh_endprologue
; LIBCALL:         .seh_startepilogue
; LIBCALL-NEXT:    addi sp, sp, 32
; LIBCALL-NEXT:    .seh_set_cfa x2, 48
; LIBCALL-NEXT:    tail __riscv_restore_4
; LIBCALL-NEXT:    .seh_endepilogue
define i64 @saves(i64 %x) {
  %a = alloca [32 x i8]
  call void asm sideeffect "", "~{s1},~{s2},~{s3}"()
  call void @g(ptr %a)
  ret i64 %x
}

; VALID-NOT: ValidationError
