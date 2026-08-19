; RUN: opt -passes="always-inline" -pass-remarks=inline \
; RUN:   -pass-remarks-missed=inline -S < %s 2>&1 \
; RUN:   | FileCheck %s --implicit-check-not="remark: "

; CHECK-DAG: remark: {{.*}} 'contract' is not inlined into 'unmarked_caller': incompatible Microsoft C++ EH4 noexcept
; CHECK-DAG: remark: {{.*}} 'contract' inlined into 'marked_caller' with (cost=always): always inline attribute
; CHECK-DAG: remark: {{.*}} 'cleanup_contract' inlined into 'marked_cleanup_caller' with (cost=always): always inline attribute
; CHECK-DAG: remark: {{.*}} 'contract' inlined into 'marked_funclet_caller' with (cost=always): always inline attribute
; CHECK-DAG: remark: {{.*}} 'cleanup_contract' is not inlined into 'marked_funclet_caller': Microsoft C++ EH4 noexcept cleanup in local-unwind funclet

declare i32 @__CxxFrameHandler4(...)
declare void @may_throw()
declare void @destroy()

define internal void @contract() #0 personality ptr @__CxxFrameHandler4 {
entry:
  call void @may_throw()
  ret void
}

define void @unmarked_caller() {
entry:
  ; CHECK-LABEL: define void @unmarked_caller()
  ; CHECK: call void @contract()
  call void @contract()
  ret void
}

define void @marked_caller() #1 personality ptr @__CxxFrameHandler4 {
entry:
  ; CHECK-LABEL: define void @marked_caller()
  ; CHECK-NOT: call void @contract()
  ; CHECK: call void @may_throw() #[[NOUNWIND:[0-9]+]]
  call void @contract()
  ret void
}

define internal void @cleanup_contract() #0 personality ptr @__CxxFrameHandler4 {
entry:
  invoke void @may_throw()
      to label %return unwind label %cleanup

cleanup:
  %pad = cleanuppad within none []
  call void @destroy() [ "funclet"(token %pad) ]
  cleanupret from %pad unwind to caller

return:
  ret void
}

define void @marked_cleanup_caller() #1 personality ptr @__CxxFrameHandler4 {
entry:
  ; The caller's FH4 frame still owns an escaping cleanup at a top-level site.
  ; CHECK-LABEL: define void @marked_cleanup_caller()
  ; CHECK-NOT: call void @cleanup_contract()
  ; CHECK: invoke void @may_throw()
  call void @cleanup_contract()
  ret void
}

define void @marked_funclet_caller() #1 personality ptr @__CxxFrameHandler4 {
entry:
  invoke void @may_throw()
      to label %return unwind label %catch.dispatch

catch.dispatch:
  %switch = catchswitch within none [label %catch] unwind label %cleanup

catch:
  %pad = catchpad within %switch []
  ; A cleanup-free marked callee remains safe and profitable here.
  call void @contract() [ "funclet"(token %pad) ]
  ; Inlining this call would turn cleanup_contract's escaping cleanupret into
  ; unreachable, losing the noexcept termination boundary.
  call void @cleanup_contract() [ "funclet"(token %pad) ]
  catchret from %pad to label %return

cleanup:
  %cleanup.pad = cleanuppad within none []
  call void @destroy() [ "funclet"(token %cleanup.pad) ]
  cleanupret from %cleanup.pad unwind to caller

return:
  ; CHECK-LABEL: define void @marked_funclet_caller()
  ; CHECK-NOT: call void @contract()
  ; CHECK: call void @cleanup_contract()
  ret void
}

; CHECK: attributes #[[NOUNWIND]] = { nounwind }

attributes #0 = { alwaysinline nounwind "msvc-cxx-eh4-noexcept" }
attributes #1 = { nounwind "msvc-cxx-eh4-noexcept" }
