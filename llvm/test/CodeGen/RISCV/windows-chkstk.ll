; RUN: llc -mtriple=riscv64-pc-windows-msvc < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu < %s | FileCheck %s

; Windows RISC-V probes stack allocations of at least one page through
; __chkstk: the size in bytes is passed in t1 and the routine is called with
; t0 as the link register. __chkstk probes and performs the allocation.

declare void @use(ptr)

define void @small() nounwind {
; CHECK-LABEL: small:
; CHECK-NOT:   __chkstk
; CHECK:       ret
  %a = alloca [3000 x i8]
  call void @use(ptr %a)
  ret void
}

define void @large() nounwind {
; CHECK-LABEL: large:
; CHECK:       call t0, __chkstk
; CHECK:       ret
  %a = alloca [8192 x i8]
  call void @use(ptr %a)
  ret void
}

define void @large_uwtable() uwtable {
; CHECK-LABEL: large_uwtable:
; CHECK:       call t0, __chkstk
; CHECK:       ret
  %a = alloca [8192 x i8]
  call void @use(ptr %a)
  ret void
}

define void @no_probe() nounwind "no-stack-arg-probe" {
; CHECK-LABEL: no_probe:
; CHECK-NOT:   __chkstk
; CHECK:       ret
  %a = alloca [8192 x i8]
  call void @use(ptr %a)
  ret void
}

define void @larger_probe_size() nounwind "stack-probe-size"="16384" {
; CHECK-LABEL: larger_probe_size:
; CHECK-NOT:   __chkstk
; CHECK:       ret
  %a = alloca [8192 x i8]
  call void @use(ptr %a)
  ret void
}

define void @dynamic(i64 %n) nounwind {
; CHECK-LABEL: dynamic:
; CHECK:       call t0, __chkstk
; CHECK:       mv {{[a-z0-9]+}}, sp
; CHECK:       ret
  %a = alloca i8, i64 %n
  call void @use(ptr %a)
  ret void
}

define void @dynamic_no_probe(i64 %n) nounwind "no-stack-arg-probe" {
; CHECK-LABEL: dynamic_no_probe:
; CHECK-NOT:   __chkstk
; CHECK:       ret
  %a = alloca i8, i64 %n
  call void @use(ptr %a)
  ret void
}
