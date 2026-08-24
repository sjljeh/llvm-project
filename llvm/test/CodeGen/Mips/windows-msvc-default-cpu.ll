; RUN: llc -mtriple=mipsel-pc-windows-msvc < %s | FileCheck %s --check-prefix=MIPS2
; RUN: llc -mtriple=mipsel-pc-windows-msvc -mcpu=mips32r2 < %s | FileCheck %s --check-prefix=MIPS32R2

define i32 @f(i32 %a, i32 %b, i32 %c) {
  %cmp = icmp ne i32 %c, 0
  %sel = select i1 %cmp, i32 %a, i32 %b
  ret i32 %sel
}

; MIPS2-LABEL: f:
; MIPS2-NOT: movn
; MIPS2: bnez
; MIPS2: jr

; MIPS32R2-LABEL: f:
; MIPS32R2: movn
; MIPS32R2: jr
