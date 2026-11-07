; RUN: llc -mtriple=riscv64-pc-windows-msvc -verify-machineinstrs < %s | FileCheck %s
;
; Under asynchronous SEH (/EHa), a faulting instruction in a __try body is
; covered by the scope-table range. The dispatcher looks up the faulting frame
; at its exact ControlPc and an unwound frame at ControlPc - 2, so no padding
; follows a call that ends a range.

declare i32 @__C_specific_handler(...)
declare void @llvm.seh.try.begin()
declare void @llvm.seh.try.end()
declare void @may_throw()

; CHECK-LABEL: fault_in_try:
; CHECK:       .Ltmp[[BEGIN:[0-9]+]]:
; CHECK-NEXT:    lw a0, 0(a0)
; CHECK-NEXT:  .Ltmp[[END:[0-9]+]]:
; CHECK:         .long .Ltmp[[BEGIN]]@IMGREL
; CHECK-NEXT:    .long .Ltmp[[END]]@IMGREL
define i32 @fault_in_try(ptr %p) personality ptr @__C_specific_handler {
entry:
  invoke void @llvm.seh.try.begin()
          to label %try unwind label %catch.dispatch

try:
  %v = load volatile i32, ptr %p, align 4
  invoke void @llvm.seh.try.end()
          to label %done unwind label %catch.dispatch

catch.dispatch:
  %cs = catchswitch within none [label %except] unwind to caller

except:
  %pad = catchpad within %cs [ptr null]
  catchret from %pad to label %done

done:
  %r = phi i32 [ %v, %try ], [ -1, %except ]
  ret i32 %r
}

; CHECK-LABEL: call_ends_try:
; CHECK:         call may_throw
; CHECK-NEXT:  .Ltmp{{[0-9]+}}:
; CHECK-NOT:     nop
; CHECK:         .seh_startepilogue
define void @call_ends_try() personality ptr @__C_specific_handler {
entry:
  invoke void @may_throw()
          to label %done unwind label %catch.dispatch

catch.dispatch:
  %cs = catchswitch within none [label %except] unwind to caller

except:
  %pad = catchpad within %cs [ptr null]
  catchret from %pad to label %done

done:
  ret void
}

!llvm.module.flags = !{!0}
!0 = !{i32 2, !"eh-asynch", i32 1}
