; RUN: llc -mtriple=i686-pc-windows-msvc -verify-machineinstrs < %s -o /dev/null

; A top-level seh_try_end can be reached in the function state (-1), which has
; no entry in SEHUnwindMap.

declare void @llvm.seh.try.end() nounwind
declare i32 @__C_specific_handler(...)

define void @top_level_try_end() personality ptr @__C_specific_handler {
entry:
  invoke void @llvm.seh.try.end()
          to label %return unwind label %cleanup

return:
  ret void

cleanup:
  %pad = cleanuppad within none []
  cleanupret from %pad unwind to caller
}

!llvm.module.flags = !{!0}
!0 = !{i32 1, !"eh-asynch", i32 1}
