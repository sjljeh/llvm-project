; RUN: llc -mtriple=mipsel-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mipsel-pc-windows-msvc -O0 < %s \
; RUN:   | FileCheck %s --check-prefix=O0

; NT commits a stack only through the guard page directly below it, so a frame
; or alloca larger than a page touches every page, top to bottom, before $sp
; moves past it. The probes store $zero through $t8 ($24) and count in $t7
; ($15): NT reverse-executes `sw r, n($sp)`, moves and the `ori`/`lui` of the
; allocation register, but none of these.

declare void @use(ptr)

; A frame of one page or less is not probed.
define void @small() "probe-stack"="inline-asm" {
; CHECK-LABEL: small:
; CHECK-NOT:   $24
; CHECK:       addiu $sp, $sp, -4024
  %b = alloca [4000 x i8]
  call void @use(ptr %b)
  ret void
}

; One probe a page below the incoming $sp, one at the new $sp.
define void @page() "probe-stack"="inline-asm" {
; CHECK-LABEL: page:
; CHECK:       addiu $24, $sp, -4096
; CHECK-NEXT:  sw $zero, 0($24)
; CHECK-NEXT:  sw $zero, -1928($24)
; CHECK-NEXT:  addiu $sp, $sp, -6024
; CHECK-NEXT:  sw $ra, 6020($sp)
  %b = alloca [6000 x i8]
  call void @use(ptr %b)
  ret void
}

; Up to four probes are unrolled.
define void @four() "probe-stack"="inline-asm" {
; CHECK-LABEL: four:
; CHECK:       addiu $24, $sp, -4096
; CHECK-NEXT:  sw $zero, 0($24)
; CHECK-NEXT:  addiu $24, $24, -4096
; CHECK-NEXT:  sw $zero, 0($24)
; CHECK-NEXT:  addiu $24, $24, -4096
; CHECK-NEXT:  sw $zero, 0($24)
; CHECK-NEXT:  addiu $24, $24, -4096
; CHECK-NEXT:  sw $zero, 0($24)
; CHECK-NEXT:  sw $zero, -3640($24)
; CHECK-NEXT:  addiu $sp, $sp, -20024
  %b = alloca [20000 x i8]
  call void @use(ptr %b)
  ret void
}

; A larger frame is allocated in two steps (callee saves first). The probe
; loop runs before the second step and covers the whole frame from the
; incoming $sp. The pointer step, not the store, fills the delay slot.
define void @split() "probe-stack"="inline-asm" {
; CHECK-LABEL: split:
; CHECK:       addiu $sp, $sp, -8
; CHECK-NEXT:  sw $ra, 4($sp)
; CHECK-NEXT:  addiu $24, $sp, -4088
; CHECK-NEXT:  addiu $15, $zero, 9
; CHECK-NEXT:  [[LOOP:\.LBB[0-9_]+]]:
; CHECK-NEXT:  sw $zero, 0($24)
; CHECK-NEXT:  addiu $15, $15, -1
; CHECK-NEXT:  bnez $15, [[LOOP]]
; CHECK-NEXT:  addiu $24, $24, -4096
; CHECK-NEXT:  # %bb
; CHECK-NEXT:  sw $zero, 936($24)
; CHECK-NEXT:  ori $1, $zero, 40016
; CHECK-NEXT:  subu $sp, $sp, $1
; CHECK-NEXT:  .seh_endprologue
; O0-LABEL:    split:
; O0:          sw $zero, 0($24)
; O0-NEXT:     addiu $15, $15, -1
; O0-NEXT:     addiu $24, $24, -4096
; O0-NEXT:     bnez $15,
; O0-NEXT:     nop
; O0:          sw $zero, 936($24)
; O0-NEXT:     ori $1, $zero, 40016
; O0-NEXT:     subu $sp, $sp, $1
  %b = alloca [40000 x i8]
  call void @use(ptr %b)
  ret void
}

; The count is materialized with lui/ori when it does not fit an addiu.
define void @huge() "probe-stack"="inline-asm" {
; CHECK-LABEL: huge:
; CHECK:       addiu $24, $sp, -4088
; CHECK-NEXT:  lui $15, 1
; CHECK-NEXT:  ori $15, $15, 0
; CHECK:       sw $zero, 4072($24)
; CHECK-NEXT:  lui $1, 4096
; CHECK-NEXT:  ori $1, $1, 16
; CHECK-NEXT:  subu $sp, $sp, $1
  %b = alloca [268435456 x i8]
  call void @use(ptr %b)
  ret void
}

; "stack-probe-size" changes the interval.
define void @interval() "probe-stack"="inline-asm" "stack-probe-size"="8192" {
; CHECK-LABEL: interval:
; CHECK:       addiu $24, $sp, -8192
; CHECK-NEXT:  sw $zero, 0($24)
; CHECK-NEXT:  sw $zero, -3832($24)
; CHECK-NEXT:  addiu $sp, $sp, -12024
  %b = alloca [12000 x i8]
  call void @use(ptr %b)
  ret void
}

; Without the attribute nothing is probed.
define void @noprobe() {
; CHECK-LABEL: noprobe:
; CHECK-NOT:   $24
; CHECK:       ori $1, $zero, 40016
  %b = alloca [40000 x i8]
  call void @use(ptr %b)
  ret void
}

; An alloca probes each page below $sp, then the page holding the new $sp
; (unless the size is zero), then moves $sp.
define void @dyn(i32 %n) "probe-stack"="inline-asm" {
; CHECK-LABEL: dyn:
; CHECK:       move $fp, $sp
; CHECK:       subu $[[NEW:[0-9]+]], $sp, $1
; CHECK-NEXT:  addiu $[[LIM:[0-9]+]], $[[NEW]], 4096
; CHECK-NEXT:  sltu $1, $[[LIM]], $sp
; CHECK-NEXT:  beqz $1, [[LAST:\.LBB[0-9_]+]]
; CHECK-NEXT:  nop
; CHECK-NEXT:  # %bb
; CHECK-NEXT:  addiu $[[CUR:[0-9]+]], $sp, -4096
; CHECK-NEXT:  [[LOOP:\.LBB[0-9_]+]]:
; CHECK-NEXT:  sw $zero, 0($[[CUR]])
; CHECK-NEXT:  sltu $1, $[[LIM]], $[[CUR]]
; CHECK-NEXT:  bnez $1, [[LOOP]]
; CHECK-NEXT:  addiu $[[CUR]], $[[CUR]], -4096
; CHECK-NEXT:  [[LAST]]:
; CHECK-NEXT:  sltu $1, $[[NEW]], $sp
; CHECK-NEXT:  beqz $1, [[DONE:\.LBB[0-9_]+]]
; CHECK-NEXT:  nop
; CHECK-NEXT:  # %bb
; CHECK-NEXT:  sw $zero, 0($[[NEW]])
; CHECK-NEXT:  [[DONE]]:
; CHECK-NEXT:  move $sp, $[[NEW]]
  %p = alloca i8, i32 %n
  call void @use(ptr %p)
  ret void
}
