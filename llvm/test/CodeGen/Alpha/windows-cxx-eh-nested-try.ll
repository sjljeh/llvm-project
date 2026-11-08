; RUN: not llc -mtriple=alpha-pc-windows-msvc < %s 2>&1 | FileCheck %s

; The NT Alpha C++ runtime treats a catch block as a nested function with its
; own FuncInfo and EH context record (it ignores catch depth). Funclets here
; share the parent's FuncInfo, so a try block inside a catch cannot be
; described; reject it rather than miscompile.
;
;   void f() { try { g(); } catch (...) { try { g(); } catch (...) {} } }

; CHECK: error: {{.*}}in function f void (): Windows Alpha C++ EH: try blocks inside catch or cleanup handlers are not supported

define void @f() personality ptr @__CxxFrameHandler {
entry:
  invoke void @g()
          to label %exit unwind label %outer.dispatch

outer.dispatch:
  %cs = catchswitch within none [label %outer.catch] unwind to caller

outer.catch:
  %cp = catchpad within %cs [ptr null, i32 64, ptr null]
  invoke void @g() [ "funclet"(token %cp) ]
          to label %outer.ret unwind label %inner.dispatch

inner.dispatch:
  %ics = catchswitch within %cp [label %inner.catch] unwind to caller

inner.catch:
  %icp = catchpad within %ics [ptr null, i32 64, ptr null]
  catchret from %icp to label %outer.ret

outer.ret:
  catchret from %cp to label %exit

exit:
  ret void
}

declare void @g()
declare i32 @__CxxFrameHandler(...)
