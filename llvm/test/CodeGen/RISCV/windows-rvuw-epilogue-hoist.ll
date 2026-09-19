; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64 < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64 -filetype=obj < %s -o %t.obj
;
; The branch folder must not hoist the identical epilogue-start markers
; from these two successors. They describe separate linear unwind scopes.
; CHECK-LABEL: conditional_tailcall:
; CHECK: .seh_startepilogue
; CHECK: .seh_endepilogue
; CHECK: .seh_startepilogue
; CHECK: .seh_endepilogue

target triple = "riscv64-w64-windows-gnu"

declare ptr @get_pointer()
declare void @release(ptr)

define void @conditional_tailcall() uwtable {
entry:
  %p = call ptr @get_pointer()
  %empty = icmp eq ptr %p, null
  br i1 %empty, label %exit, label %tail
tail:
  tail call void @release(ptr %p)
  ret void
exit:
  ret void
}
