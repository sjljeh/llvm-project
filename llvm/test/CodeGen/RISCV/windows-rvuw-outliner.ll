; RUN: llc -mtriple=riscv64-pc-windows-msvc -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-pc-windows-msvc -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
;
; The machine outliner never moves RVUW rules or the frame setup and teardown
; they describe. An outlined body reached through `call t0` gets its own
; record, which returns through t0.

declare void @h()

define void @a(ptr %p) minsize {
  store volatile i64 3, ptr %p
  store volatile i64 10, ptr %p
  store volatile i64 17, ptr %p
  store volatile i64 24, ptr %p
  store volatile i64 31, ptr %p
  store volatile i64 38, ptr %p
  call void @h()
  store volatile i64 3, ptr %p
  store volatile i64 10, ptr %p
  store volatile i64 17, ptr %p
  store volatile i64 24, ptr %p
  store volatile i64 31, ptr %p
  store volatile i64 38, ptr %p
  ret void
}

define void @b(ptr %p) minsize {
  store volatile i64 3, ptr %p
  store volatile i64 10, ptr %p
  store volatile i64 17, ptr %p
  store volatile i64 24, ptr %p
  store volatile i64 31, ptr %p
  store volatile i64 38, ptr %p
  call void @h()
  store volatile i64 3, ptr %p
  store volatile i64 10, ptr %p
  store volatile i64 17, ptr %p
  store volatile i64 24, ptr %p
  store volatile i64 31, ptr %p
  store volatile i64 38, ptr %p
  ret void
}

; CHECK-LABEL: a:
; CHECK:         .seh_endprologue
; CHECK:         call t0, OUTLINED_FUNCTION_0
; CHECK:         .seh_startepilogue
; CHECK-LABEL: OUTLINED_FUNCTION_0:
; CHECK-NEXT:  .seh_proc OUTLINED_FUNCTION_0
; CHECK-NEXT:  # %bb.0:
; CHECK-NEXT:    .seh_return_from x5
; CHECK-NEXT:    .seh_endprologue
; CHECK:         jr t0
; CHECK-NEXT:    .seh_endproc

; UNWIND-NOT:  ValidationError
; UNWIND:      Opcode: RETURN_FROM_GPR
; UNWIND-NEXT: Register: x5 (5)
; UNWIND-NOT:  ValidationError
