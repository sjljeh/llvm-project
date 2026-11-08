; NTUW unwind rules for MIPS frames. The
; prologue shapes are NT 4.0's; the rules describe them at every instruction.
; RUN: llc -mtriple=mipsel-pc-windows-msvc19.33 < %s | FileCheck %s
; RUN: llc -mtriple=mipsel-pc-windows-msvc19.33 -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=OBJ
; An unversioned or pre-VC 7.0 triple keeps the NT 4.0 format.
; RUN: llc -mtriple=mipsel-pc-windows-msvc12.0 < %s \
; RUN:   | FileCheck %s --check-prefix=LEGACY --implicit-check-not=.seh_set_cfa

declare void @g(ptr)

define void @simple() {
  %a = alloca [4 x i32]
  call void @g(ptr %a)
  ret void
}

define void @big() {
  %a = alloca [40000 x i8]
  call void @g(ptr %a)
  ret void
}

define void @dyn(i32 %n) {
  %a = alloca i8, i32 %n
  call void @g(ptr %a)
  ret void
}

; CHECK-LABEL: simple:
; CHECK:       addiu $sp, $sp, -40
; CHECK-NEXT:  .seh_set_cfa 29, 40
; CHECK-NEXT:  sw $ra, 36($sp)
; CHECK-NEXT:  .seh_save_gpr 31, -4
; CHECK-NEXT:  .seh_endprologue
; CHECK:       .seh_startepilogue
; CHECK-NEXT:  lw $ra, 36($sp)
; CHECK-NEXT:  .seh_same_gpr 31
; CHECK-NEXT:  jr $ra
; CHECK-NEXT:  addiu $sp, $sp, 40
; CHECK-NEXT:  .seh_set_cfa 29, 0
; CHECK-NEXT:  .seh_endepilogue

; A frame too large for one addiu is allocated in two steps; the establisher
; frame stays where NT 4.0 latches it, below the first step.
; CHECK-LABEL: big:
; CHECK:       addiu $sp, $sp, -8
; CHECK-NEXT:  .seh_set_cfa 29, 8
; CHECK-NEXT:  sw $ra, 4($sp)
; CHECK-NEXT:  .seh_save_gpr 31, -4
; CHECK-NEXT:  ori $1, $zero, 40016
; CHECK-NEXT:  subu $sp, $sp, $1
; CHECK-NEXT:  .seh_set_cfa 29, 40024
; CHECK-NEXT:  .seh_establisher cfa, -8
; CHECK-NEXT:  .seh_endprologue

; With alloca, the CFA moves to $fp, and back to $sp before $fp is restored.
; CHECK-LABEL: dyn:
; CHECK:       move $fp, $sp
; CHECK-NEXT:  .seh_set_cfa 30, 16
; CHECK-NEXT:  .seh_endprologue
; CHECK:       move $sp, $fp
; CHECK-NEXT:  .seh_startepilogue
; CHECK-NEXT:  lw $ra, 12($sp)
; CHECK-NEXT:  .seh_same_gpr 31
; CHECK-NEXT:  lw $fp, 8($sp)
; CHECK-NEXT:  .seh_set_cfa 29, 16
; CHECK-NEXT:  .seh_same_gpr 30
; CHECK-NEXT:  jr $ra
; CHECK-NEXT:  addiu $sp, $sp, 16
; CHECK-NEXT:  .seh_set_cfa 29, 0

; OBJ:      UnwindFormat: NTUW
; OBJ-COUNT-3: Magic: 0x5755544E
; OBJ-NOT:  ValidationError

; LEGACY-LABEL: simple:
; LEGACY:       .seh_endprologue
