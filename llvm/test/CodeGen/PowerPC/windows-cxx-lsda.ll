; Copyright (c) 2026 Ahmed ARIF

; REQUIRES: powerpc-registered-target
; RUN: llc -mtriple=powerpcle-w64-windows-gnu < %s | FileCheck %s
; RUN: llc -mtriple=powerpcle-w64-windows-gnu -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj -r %t.obj | FileCheck %s --check-prefix=OBJ

; The HandlerData word of the Windows NT PowerPC function table entry points
; at the function's .xdata record. As on AMD64 and ARM64, the Itanium LSDA
; must be emitted there rather than in .gcc_except_table.
; CHECK: .seh_handler __gxx_personality_seh0, @unwind, @except
; CHECK: bl ..may_throw
; CHECK-NEXT: .znop ..may_throw
; CHECK: .seh_handlerdata
; CHECK: .section .xdata
; CHECK-NOT: .section .gcc_except_table
; CHECK: GCC_except_table
; CHECK: .byte 255

; OBJ:      Section ({{[0-9]+}}) .pdata {
; OBJ-NEXT:   IMAGE_REL_PPC_ADDR32 .text
; OBJ-NEXT:   IMAGE_REL_PPC_ADDR32 .text
; OBJ-NEXT:   IMAGE_REL_PPC_ADDR32 __gxx_personality_seh0
; OBJ-NEXT:   IMAGE_REL_PPC_ADDR32 .xdata
; OBJ-NEXT:   IMAGE_REL_PPC_ADDR32 .text

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
