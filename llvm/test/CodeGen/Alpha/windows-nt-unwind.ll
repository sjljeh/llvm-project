; RUN: llc -mtriple=alpha-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 -filetype=obj < %s -o %t.obj

; NT unwinds by reverse-executing the prologue. It recovers registers only from
; `stq/stt r, n($30)` with a 16-bit n, reverses `lda/ldah $30, n($30)` and
; `bis $30, $30, $15`, and recognizes an epilogue only at the `ret` or at a
; single `lda $30` or `addq $30, Rx, $30` directly before it.

declare void @use(ptr)
declare i64 @callee(i64)

; The callee-saved area is allocated and saved before the rest of a large
; frame, and the frame is released by one addq before the return.
; CHECK-LABEL: big_frame:
; CHECK:      lda $30,-32($30)
; CHECK-NEXT: stq $26,24($30)
; CHECK-NEXT: stq $9,16($30)
; CHECK-NEXT: stq $10,8($30)
; CHECK-NEXT: ldah $30,-1($30)
; CHECK-NEXT: lda $30,25536($30)
; CHECK-NEXT: .seh_endprologue
; CHECK:      ldah $28,1($31)
; CHECK-NEXT: lda $28,-25504($28)
; CHECK-NEXT: addq $30,$28,$30
; CHECK-NEXT: ret
define i64 @big_frame(i64 %n) {
  %buf = alloca [40000 x i8], align 1
  call void @use(ptr %buf)
  %p = getelementptr i8, ptr %buf, i64 %n
  %v = load i8, ptr %p
  %r = sext i8 %v to i64
  ret i64 %r
}

; The frame pointer copy follows every prologue store. The epilogue moves it
; back into $30 before the reloads, which address their slots from $30, and
; reloads $15 last.
; CHECK-LABEL: dyn_frame:
; CHECK:      lda $30,-32($30)
; CHECK-NEXT: stq $15,0($30)
; CHECK-NEXT: stq $26,24($30)
; CHECK-NEXT: bis $30,$30,$15
; CHECK-NEXT: .seh_endprologue
; CHECK:      bis $15,$15,$30
; CHECK-NEXT: ldq $26,24($30)
; CHECK-NEXT: ldq $15,0($30)
; CHECK-NEXT: lda $30,32($30)
; CHECK-NEXT: ret
define i64 @dyn_frame(i64 %n) {
  %p = alloca i8, i64 %n, align 8
  call void @use(ptr %p)
  ret i64 0
}

; CHECK-LABEL: big_dyn_frame:
; CHECK:      lda $30,-16($30)
; CHECK-NEXT: stq $26,8($30)
; CHECK-NEXT: stq $9,0($30)
; CHECK-NEXT: ldah $30,-1($30)
; CHECK-NEXT: lda $30,25520($30)
; CHECK-NEXT: stq $15,0($30)
; CHECK-NEXT: bis $30,$30,$15
; CHECK-NEXT: .seh_endprologue
; CHECK:      ldah $28,1($31)
; CHECK-NEXT: lda $28,-25504($28)
; CHECK-NEXT: ldq $15,0($30)
; CHECK-NEXT: addq $30,$28,$30
; CHECK-NEXT: ret
define i64 @big_dyn_frame(i64 %n) {
  %buf = alloca [40000 x i8], align 1
  %p = alloca i8, i64 %n, align 8
  call void @use(ptr %buf)
  call void @use(ptr %p)
  ret i64 0
}

; $26 is saved in the prologue, not carried in a callee-saved register, and a
; call is not a terminator: the reloads follow it, even at -O0.
; CHECK-LABEL: call_result:
; CHECK:      lda $30,-16($30)
; CHECK-NEXT: stq $26,8($30)
; CHECK-NEXT: .seh_endprologue
; CHECK-NEXT: bsr $26,$callee..ng
; CHECK-NEXT: ldq $26,8($30)
; CHECK-NEXT: lda $30,16($30)
; CHECK-NEXT: ret
; O0-LABEL: call_result:
; O0:      bsr $26,$callee..ng
; O0-NEXT: ldq $26,8($30)
; O0-NEXT: lda $30,16($30)
; O0-NEXT: ret
define i64 @call_result(i64 %a) {
  %r = call i64 @callee(i64 %a)
  ret i64 %r
}
