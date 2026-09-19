; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64 < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64 -filetype=obj < %s -o %t.obj
;
; The RVUW handler-data RVA points immediately after the unwind record.
; As on ARM64 and AMD64, the Itanium LSDA must remain in that xdata section.
; CHECK: .seh_handler __gxx_personality_seh0
; CHECK: .seh_handlerdata
; CHECK: .section .xdata
; CHECK-NOT: .section .gcc_except_table
; CHECK: GCC_except_table
; CHECK: .byte 255

declare i32 @__gxx_personality_seh0(...)
declare void @may_throw()

define void @catch_all() uwtable personality ptr @__gxx_personality_seh0 {
entry:
  invoke void @may_throw() to label %done unwind label %catch
catch:
  %exception = landingpad {ptr, i32} catch ptr null
  br label %done
done:
  ret void
}
