; Copyright (c) 2026 Ahmed ARIF

; RUN: llc -mtriple=powerpcle-pc-windows-gnu < %s | FileCheck %s

; Windows NT PowerPC passes an __int64 in an eight-byte aligned parameter
; slot: the register pair starts at an odd register. Clang pads named
; arguments; variadic arguments and libcalls get the alignment here.

declare void @vararg(ptr, ...)
declare i64 @helper(ptr, i64, i32)

; CHECK-LABEL: call_vararg:
; CHECK-DAG: li 5, 2
; CHECK-DAG: li 6, 0
; CHECK: bl ..vararg
define void @call_vararg(ptr %p) {
  call void (ptr, ...) @vararg(ptr %p, i64 2)
  ret void
}

; CHECK-LABEL: call_helper:
; CHECK-DAG: mr 5, 4
; CHECK-DAG: li 7, 5
; CHECK: bl ..helper
define i64 @call_helper(ptr %p, i32 %lo) {
  %v = zext i32 %lo to i64
  %r = call i64 @helper(ptr %p, i64 %v, i32 5)
  ret i64 %r
}

; CHECK-LABEL: rmw:
; CHECK: li 7, 5
; CHECK: bl ..__atomic_fetch_add_8
define i64 @rmw(ptr %p, i64 %v) {
  %r = atomicrmw add ptr %p, i64 %v seq_cst
  ret i64 %r
}
