; RUN: llc -mtriple=alpha-unknown-linux-gnu < %s | FileCheck %s

; Keep this module free of an explicit data layout so that the Alpha target
; machine supplies and exercises its ELF layout.
target triple = "alpha-unknown-linux-gnu"

@g64 = global i64 0
@g128 = global i128 0
@gquad = global fp128 0.0

; CHECK:      .type g64,@object
; CHECK:      .p2align 3, 0x0
; CHECK-NEXT: g64:
; CHECK:      .size g64, 8

; CHECK:      .type g128,@object
; CHECK:      .p2align 4, 0x0
; CHECK-NEXT: g128:
; CHECK:      .size g128, 16

; CHECK:      .type gquad,@object
; CHECK:      .p2align 4, 0x0
; CHECK-NEXT: gquad:
; CHECK:      .size gquad, 16
