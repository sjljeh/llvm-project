; RUN: llc -mtriple=alpha-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 -filetype=obj < %s -o %t.obj

; NT commits a stack only through the guard page directly below it, so a frame
; or alloca larger than a page touches every page, top to bottom, before $30
; moves past it. NT does not reverse-execute `stq $31, n($30)`, stores through
; other bases, or lda/ldah/subq into $28 and $22, which the probes use.

declare void @use(ptr)

; A frame of one page or less is not probed.
define void @small() "probe-stack"="inline-asm" {
; CHECK-LABEL: small:
; CHECK-NOT:   stq $31
; CHECK:       lda $30,-4016($30)
  %b = alloca [4000 x i8]
  call void @use(ptr %b)
  ret void
}

; Probes within reach of $30 store $31 through it directly: one a page below
; the incoming $30 and one at the new $30.
define void @page() "probe-stack"="inline-asm" {
; CHECK-LABEL: page:
; CHECK:       stq $31,-4096($30)
; CHECK-NEXT:  stq $31,-6016($30)
; CHECK-NEXT:  lda $30,-6016($30)
; CHECK-NEXT:  stq $26,6008($30)
  %b = alloca [6000 x i8]
  call void @use(ptr %b)
  ret void
}

define void @four() "probe-stack"="inline-asm" {
; CHECK-LABEL: four:
; CHECK:       stq $31,-4096($30)
; CHECK-NEXT:  stq $31,-8192($30)
; CHECK-NEXT:  stq $31,-12288($30)
; CHECK-NEXT:  stq $31,-16384($30)
; CHECK-NEXT:  stq $31,-20016($30)
; CHECK-NEXT:  lda $30,-20016($30)
  %b = alloca [20000 x i8]
  call void @use(ptr %b)
  ret void
}

; A frame beyond 32 KB is allocated in two steps (callee saves first). The
; probe loop runs before the second step and covers the whole frame from the
; incoming $30. Its `ldah $30` rounds to the nearest 64 KB and briefly takes
; $30 below the final value, so the last probe is at that lowest point.
define void @split() "probe-stack"="inline-asm" {
; CHECK-LABEL: split:
; CHECK:       lda $30,-16($30)
; CHECK-NEXT:  stq $26,8($30)
; CHECK-NEXT:  lda $28,-4080($30)
; CHECK-NEXT:  lda $22,16($31)
; CHECK-NEXT:  {{^}}.LBB
; CHECK-NEXT:  # Label of block must be emitted
; CHECK-NEXT:  stq $31,0($28)
; CHECK-NEXT:  subq $22,1,$22
; CHECK-NEXT:  lda $28,-4096($28)
; CHECK-NEXT:  bne
; CHECK-NEXT:  {{^}}.LBB
; CHECK-NEXT:  stq $31,4080($28)
; CHECK-NEXT:  ldah $30,-1($30)
; CHECK-NEXT:  lda $30,25536($30)
; CHECK-NEXT:  .seh_endprologue
; O0-LABEL:    split:
; O0:          stq $31,0($28)
; O0-NEXT:     subq $22,1,$22
; O0-NEXT:     lda $28,-4096($28)
; O0-NEXT:     bne
; O0:          stq $31,4080($28)
; O0-NEXT:     ldah $30,-1($30)
; O0-NEXT:     lda $30,25536($30)
  %b = alloca [40000 x i8]
  call void @use(ptr %b)
  ret void
}

; Without an ldah overshoot, the last probe is at the new $30.
define void @huge() "probe-stack"="inline-asm" {
; CHECK-LABEL: huge:
; CHECK:       lda $28,-4080($30)
; CHECK-NEXT:  lda $22,48($31)
; CHECK:       stq $31,688($28)
; CHECK-NEXT:  ldah $30,-3($30)
; CHECK-NEXT:  lda $30,-3392($30)
  %b = alloca [200000 x i8]
  call void @use(ptr %b)
  ret void
}

; The count is materialized with ldah/lda when it does not fit an lda.
define void @giant() "probe-stack"="inline-asm" {
; CHECK-LABEL: giant:
; CHECK:       lda $28,-4080($30)
; CHECK-NEXT:  ldah $22,1($31)
; CHECK-NEXT:  lda $22,0($22)
; CHECK:       ldah $30,-4096($30)
  %b = alloca [268435456 x i8]
  call void @use(ptr %b)
  ret void
}

; "stack-probe-size" changes the interval.
define void @interval() "probe-stack"="inline-asm" "stack-probe-size"="8192" {
; CHECK-LABEL: interval:
; CHECK:       stq $31,-8192($30)
; CHECK-NEXT:  stq $31,-12016($30)
; CHECK-NEXT:  lda $30,-12016($30)
  %b = alloca [12000 x i8]
  call void @use(ptr %b)
  ret void
}

; Without the attribute nothing is probed.
define void @noprobe() {
; CHECK-LABEL: noprobe:
; CHECK-NOT:   stq $31
; CHECK:       ldah $30,-1($30)
  %b = alloca [40000 x i8]
  call void @use(ptr %b)
  ret void
}

; An alloca probes each page below $30, then the page holding the new $30
; (unless the size is zero), then moves $30.
define void @dyn(i64 %n) "probe-stack"="inline-asm" {
; CHECK-LABEL: dyn:
; CHECK:       bis $30,$30,$15
; CHECK:       subq $30,{{\$[0-9]+}},$[[NEW:[0-9]+]]
; CHECK-NEXT:  lda $[[LIM:[0-9]+]],4096($[[NEW]])
; CHECK-NEXT:  lda $[[CUR:[0-9]+]],-4096($30)
; CHECK-NEXT:  cmpult $[[LIM]],$30,$[[C:[0-9]+]]
; CHECK-NEXT:  beq
; CHECK:       stq $31,0($[[CUR]])
; CHECK-NEXT:  cmpult $[[LIM]],$[[CUR]],$[[C]]
; CHECK-NEXT:  lda $[[CUR]],-4096($[[CUR]])
; CHECK-NEXT:  bne
; CHECK:       cmpult $[[NEW]],$30,
; CHECK-NEXT:  beq
; CHECK:       stq $31,0($[[NEW]])
; CHECK:       bis $[[NEW]],$[[NEW]],$30
  %p = alloca i8, i64 %n
  call void @use(ptr %p)
  ret void
}
