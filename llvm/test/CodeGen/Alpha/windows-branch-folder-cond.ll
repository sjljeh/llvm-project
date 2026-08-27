; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -stop-after=postrapseudos < %s -o - | FileCheck %s

define i32 @two_step(ptr %p) {
entry:
  %p4 = getelementptr i8, ptr %p, i32 4
  %c4 = load i8, ptr %p4
  %eq4 = icmp eq i8 %c4, 107
  br i1 %eq4, label %next, label %miss

next:
  %p3 = getelementptr i8, ptr %p, i32 3
  %c3 = load i8, ptr %p3
  %eq3 = icmp eq i8 %c3, 99
  br i1 %eq3, label %hit, label %miss

hit:
  ret i32 4

miss:
  ret i32 99
}

; CHECK-LABEL: name: two_step
; CHECK: $r0 = CMPEQi killed $r0, 107
; CHECK-NEXT: BEQ {{(killed )?}}$r0, %bb.3
; CHECK: $r0 = CMPEQi killed $r0, 99
; CHECK-NEXT: BEQ {{(killed )?}}$r0, %bb.3
