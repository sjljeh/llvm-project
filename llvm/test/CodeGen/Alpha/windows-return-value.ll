; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -stop-after=postrapseudos < %s -o - | FileCheck %s

define i32 @ret_i32(i32 %x) {
entry:
  %cmp = icmp ne i32 %x, 42
  %ret = zext i1 %cmp to i32
  ret i32 %ret
}

; CHECK-LABEL: name: ret_i32
; CHECK: $r0 = CMPEQ
; CHECK: $r0 = CMPEQi
; CHECK: RETDAGp {{.*}}implicit $r0
