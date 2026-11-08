; RUN: llc -mtriple=mipsel-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mipsel-pc-windows-msvc -O0 < %s \
; RUN:   | FileCheck %s --check-prefix=O0

; NT unwinds by reverse-executing the prologue. It recognizes only
; `addiu $sp, $sp, -n`, `ori t, $zero, n` or `lui t, hi; ori t, t, lo` before
; `subu $sp, $sp, t`, `sw r, n($sp)` saves, and `move $fp, $sp`. It treats
; every PC before `jr $ra` as body code and only an $sp release in the
; delay slot of `jr $ra` as the epilogue.

declare void @use(ptr)

; A frame beyond addiu's range is allocated in two steps so that the saves
; reach their slots from the intermediate $sp.
; CHECK-LABEL: big_frame:
; CHECK:      addiu $sp, $sp, -16
; CHECK-NEXT: sw $ra, 12($sp)
; CHECK-NEXT: sw $17, 8($sp)
; CHECK-NEXT: sw $16, 4($sp)
; CHECK-NEXT: ori $1, $zero, 40016
; CHECK-NEXT: subu $sp, $sp, $1
; CHECK-NEXT: .seh_endprologue
; CHECK:      ori $1, $zero, 40032
; CHECK-NEXT: jr $ra
; CHECK-NEXT: addu $sp, $sp, $1
define i32 @big_frame(i32 %n) {
  %buf = alloca [40000 x i8], align 1
  call void @use(ptr %buf)
  %p = getelementptr i8, ptr %buf, i32 %n
  %v = load i8, ptr %p
  %r = sext i8 %v to i32
  ret i32 %r
}

; NT takes the low half of the size only from an ori.
; CHECK-LABEL: huge_frame:
; CHECK:      sw $16, 4($sp)
; CHECK-NEXT: lui $1, 3
; CHECK-NEXT: ori $1, $1, 3408
; CHECK-NEXT: subu $sp, $sp, $1
; CHECK-NEXT: .seh_endprologue
define i32 @huge_frame(i32 %n) {
  %buf = alloca [200000 x i8], align 1
  call void @use(ptr %buf)
  %p = getelementptr i8, ptr %buf, i32 %n
  %v = load i8, ptr %p
  %r = sext i8 %v to i32
  ret i32 %r
}

; $fp is reloaded last, so that `move $fp, $sp` reverse-executes correctly at
; every epilogue PC, and the frame is released in the delay slot even at -O0.
; CHECK-LABEL: dyn_frame:
; CHECK:      move $sp, $fp
; CHECK-NEXT: lw $ra, 12($sp)
; CHECK-NEXT: lw $fp, 8($sp)
; CHECK-NEXT: jr $ra
; CHECK-NEXT: addiu $sp, $sp, 16
; O0-LABEL: dyn_frame:
; O0:      move $sp, $fp
; O0-NEXT: lw $ra, 12($sp)
; O0-NEXT: lw $fp, 8($sp)
; O0-NEXT: jr $ra
; O0-NEXT: addiu $sp, $sp, 16
define i32 @dyn_frame(i32 %n) {
  %p = alloca i8, i32 %n, align 8
  call void @use(ptr %p)
  ret i32 0
}

; With a frame pointer and a large frame, $fp is reloaded through the incoming
; $sp after the frame size is formed, directly before the return.
; CHECK-LABEL: big_dyn_frame:
; CHECK:      ori $1, $zero, 40000
; CHECK-NEXT: subu $sp, $sp, $1
; CHECK-NEXT: move $fp, $sp
; CHECK-NEXT: .seh_endprologue
; CHECK:      ori $1, $zero, 40016
; CHECK-NEXT: addu $3, $sp, $1
; CHECK-NEXT: lw $fp, -8($3)
; CHECK-NEXT: jr $ra
; CHECK-NEXT: addu $sp, $sp, $1
define i32 @big_dyn_frame(i32 %n) {
  %buf = alloca [40000 x i8], align 1
  %p = alloca i8, i32 %n, align 8
  call void @use(ptr %buf)
  call void @use(ptr %p)
  ret i32 0
}
