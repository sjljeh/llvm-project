; RUN: llc -mtriple=alpha-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=alpha-unknown-linux-gnu -O2 < %s | FileCheck %s --check-prefix=ELF

; Objects aligned beyond the 16-byte stack alignment get a realigned frame.
; The frame size is a multiple of the alignment A. The prologue keeps the
; unaligned $30 in $15 with `bis $30, $30, $15`, the last instruction that NT
; reverse-executes, then rounds $15 down to A with `bic` or
; `and`, which RtlVirtualUnwind ignores, before PrologEnd. The epilogue moves
; $15 back into $30 before the register reloads, reloads $15 last and releases
; the frame directly before the ret.

declare void @use(ptr)
declare void @many(i64, i64, i64, i64, i64, i64, i64, i64, ptr)

define i32 @align64(i64 %n) nounwind uwtable {
; CHECK-LABEL: align64:
; CHECK:       lda $30,-192($30)
; CHECK-NEXT:  stq $15,0($30)
; CHECK-NEXT:  stq $26,184($30)
; CHECK-NEXT:  stq $9,176($30)
; CHECK-NEXT:  bis $30,$30,$15
; CHECK-NEXT:  bic $15,63,$30
; CHECK-NEXT:  .seh_endprologue
; CHECK:       lda $9,64($30)
; CHECK:       bsr $26,$use..ng
; CHECK:       bis $15,$15,$30
; CHECK-NEXT:  ldq $9,176($30)
; CHECK-NEXT:  ldq $26,184($30)
; CHECK-NEXT:  ldq $15,0($30)
; CHECK-NEXT:  lda $30,192($30)
; CHECK-NEXT:  ret
; O0-LABEL:    align64:
; O0:          bis $30,$30,$15
; O0-NEXT:     bic $15,63,$30
; O0-NEXT:     .seh_endprologue
; ELF-LABEL:   align64:
; ELF:         lda $30,-192($30)
; ELF:         stq $15,0($30)
; ELF:         bis $30,$30,$15
; ELF:         stq $9,{{[0-9]+}}($15)
; ELF:         bic $15,63,$30
; ELF:         bis $15,$15,$30
; ELF-NEXT:    ldq $15,0($15)
; ELF-NEXT:    lda $30,192($30)
; ELF-NEXT:    ret
  %b = alloca [64 x i8], align 64
  %p = getelementptr i8, ptr %b, i64 %n
  store i8 2, ptr %p
  call void @use(ptr %b)
  %q = getelementptr i8, ptr %b, i64 1
  %v = load i8, ptr %q
  %r = sext i8 %v to i32
  ret i32 %r
}

; An alignment above 256 is too wide for a `bic` literal: $28 = -A.
define void @align512() nounwind uwtable {
; CHECK-LABEL: align512:
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  lda $28,-512($31)
; CHECK-NEXT:  and $15,$28,$30
; CHECK-NEXT:  .seh_endprologue
; CHECK:       lda $16,512($30)
  %b = alloca [100 x i8], align 512
  call void @use(ptr %b)
  ret void
}

define void @align64k() nounwind uwtable {
; CHECK-LABEL: align64k:
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  ldah $28,-1($31)
; CHECK-NEXT:  and $15,$28,$30
; CHECK-NEXT:  .seh_endprologue
  %b = alloca [16 x i8], align 65536
  call void @use(ptr %b)
  ret void
}

; A leaf realigns too, and still sets $15 without unwind information.
define i32 @leaf(i64 %n) nounwind {
; CHECK-LABEL: leaf:
; CHECK:       lda $30,-64($30)
; CHECK-NEXT:  stq $15,0($30)
; CHECK-NEXT:  bis $30,$30,$15
; CHECK-NEXT:  bic $15,31,$30
; CHECK:       bis $15,$15,$30
; CHECK-NEXT:  ldq $15,0($30)
; CHECK-NEXT:  lda $30,64($30)
; CHECK-NEXT:  ret
  %b = alloca [8 x i32], align 32
  %p = getelementptr i32, ptr %b, i64 %n
  store volatile i32 1, ptr %p
  %v = load volatile i32, ptr %b
  ret i32 %v
}

; Without alloca $30 stays aligned around calls (a reserved call frame), so
; outgoing arguments start at the aligned $30, possibly below the allocated
; one. $15 is saved above the outgoing argument area (24 bytes here).
define void @stack_args() nounwind uwtable {
; CHECK-LABEL: stack_args:
; CHECK:       lda $30,-{{[0-9]+}}($30)
; CHECK-NEXT:  stq $15,24($30)
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  bic $15,63,$30
; CHECK-NEXT:  .seh_endprologue
; CHECK-NOT:   lda $30
; CHECK:       stq ${{[0-9]+}},0($30)
; CHECK:       bsr $26,$many..ng
; CHECK:       bis $15,$15,$30
; CHECK:       ldq $15,24($30)
; CHECK-NEXT:  lda $30,{{[0-9]+}}($30)
; CHECK-NEXT:  ret
  %b = alloca [4 x i64], align 64
  call void @many(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8, ptr %b)
  ret void
}

; With alloca, objects are addressed from the base pointer $14, a callee-saved
; register set to the aligned base, also with `bic`.
define i32 @realign_alloca(i64 %n) nounwind uwtable {
; CHECK-LABEL: realign_alloca:
; CHECK:       stq $14,[[BP:[0-9]+]]($30)
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  bic $15,63,$30
; CHECK-NEXT:  bic $15,63,$14
; CHECK-NEXT:  .seh_endprologue
; CHECK:       subq $30,
; CHECK:       lda ${{[0-9]+}},64($14)
; CHECK:       bis $15,$15,$30
; CHECK:       ldq $14,[[BP]]($30)
; CHECK:       ldq $15,0($30)
; ELF-LABEL:   realign_alloca:
; ELF:         bis $30,$30,$15
; ELF:         stq $14,{{[0-9]+}}($15)
; ELF:         bic $15,63,$30
; ELF-NEXT:    bic $15,63,$14
  %b = alloca [64 x i8], align 64
  %d = alloca i8, i64 %n
  call void @use(ptr %d)
  call void @use(ptr %b)
  %v = load i8, ptr %b
  %r = sext i8 %v to i32
  ret i32 %r
}

; An over-aligned alloca rounds its own address; the frame is realigned too.
define void @aligned_alloca(i64 %n) nounwind uwtable {
; CHECK-LABEL: aligned_alloca:
; CHECK:       bic $15,255,$30
; CHECK-NEXT:  bic $15,255,$14
; CHECK-NEXT:  .seh_endprologue
; CHECK:       subq $30,
; CHECK-NEXT:  bic ${{[0-9]+}},255,
  %d = alloca i8, i64 %n, align 256
  call void @use(ptr %d)
  ret void
}

; A probed frame probes down to the lowest $30 that realignment can produce,
; NumBytes + A - 16: here 4096 + 48.
define void @probed() nounwind uwtable "probe-stack"="inline-asm" {
; CHECK-LABEL: probed:
; CHECK:       stq $31,-4096($30)
; CHECK-NEXT:  stq $31,-4144($30)
; CHECK-NEXT:  lda $30,-4096($30)
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  bic $15,63,$30
; CHECK-NEXT:  .seh_endprologue
  %b = alloca [4000 x i8], align 64
  call void @use(ptr %b)
  ret void
}

; A frame that stays within one page after realignment is not probed:
; 4032 + 48 <= 4096.
define void @unprobed() nounwind uwtable "probe-stack"="inline-asm" {
; CHECK-LABEL: unprobed:
; CHECK-NOT:   stq $31
; CHECK:       lda $30,-4032($30)
  %b = alloca [3900 x i8], align 64
  call void @use(ptr %b)
  ret void
}
