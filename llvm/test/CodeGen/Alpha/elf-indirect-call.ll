; RUN: llc -mtriple=alpha-unknown-linux-gnu -filetype=obj %s -o - | llvm-readobj --relocations - | FileCheck %s

; An indirect JSR must not carry LITUSE_JSR.  GNU Alpha associates LITUSE with
; the preceding LITERAL, which in this function is an unrelated data load.
@g = external global i64

define i64 @indirect(ptr %callee) {
entry:
  %v = load i64, ptr @g, align 8
  %r = call i64 %callee(i64 %v)
  ret i64 %r
}

; CHECK: R_ALPHA_LITERAL g 0x0
; CHECK-NOT: R_ALPHA_LITUSE
