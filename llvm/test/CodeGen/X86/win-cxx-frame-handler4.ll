; RUN: llc -verify-machineinstrs -mtriple=x86_64-pc-windows-msvc < %s | FileCheck %s
; RUN: llc -verify-machineinstrs -mtriple=x86_64-pc-windows-msvc -filetype=obj < %s -o %t
; RUN: llvm-readobj --symbols %t | FileCheck %s --check-prefix=OBJ

declare void @f(i32)
declare void @destroy(ptr) nounwind
declare void @terminate() nounwind
declare i32 @__CxxFrameHandler4(...)

define i32 @try_in_catch() personality ptr @__CxxFrameHandler4 {
entry:
  invoke void @f(i32 1)
          to label %try.cont unwind label %catch.dispatch.1

try.cont:
  ret i32 0

catch.dispatch.1:
  %cs1 = catchswitch within none [label %handler1] unwind to caller

handler1:
  %h1 = catchpad within %cs1 [ptr null, i32 64, ptr null]
  invoke void @f(i32 2) [ "funclet"(token %h1) ]
          to label %catchret1 unwind label %catch.dispatch.2

catchret1:
  catchret from %h1 to label %try.cont

catch.dispatch.2:
  %cs2 = catchswitch within %h1 [label %handler2] unwind to caller

handler2:
  %h2 = catchpad within %cs2 [ptr null, i32 64, ptr null]
  call void @f(i32 3) [ "funclet"(token %h2) ]
  catchret from %h2 to label %catchret1
}

define void @cleanup() personality ptr @__CxxFrameHandler4 {
entry:
  %object = alloca i8
  invoke void @f(i32 4)
          to label %return unwind label %cleanup.pad

return:
  call void @destroy(ptr %object)
  ret void

cleanup.pad:
  %cleanup = cleanuppad within none []
  call void @destroy(ptr %object) [ "funclet"(token %cleanup) ]
  cleanupret from %cleanup unwind to caller
}

define void @cannot_throw() nounwind personality ptr @__CxxFrameHandler4 {
entry:
  invoke void @f(i32 5)
          to label %return unwind label %terminate.pad

return:
  ret void

terminate.pad:
  %cleanup = cleanuppad within none []
  call void @terminate() [ "funclet"(token %cleanup) ]
  unreachable
}

; CHECK-LABEL: try_in_catch:
; CHECK:       .seh_handler __CxxFrameHandler4, @unwind, @except
; CHECK-NOT:   movq $-2
; CHECK:       "?catch$[[OUTER:[0-9]+]]@?0?try_in_catch@4HA":
; CHECK:       .seh_handler __CxxFrameHandler4, @unwind, @except
; CHECK:       "?catch$[[INNER:[0-9]+]]@?0?try_in_catch@4HA":
; CHECK-NOT:   .seh_handler __CxxFrameHandler4
; CHECK:       $cppxdata$try_in_catch:
; CHECK-NEXT:  .byte 56
; CHECK-NEXT:  .long $stateUnwindMap$try_in_catch@IMGREL
; CHECK-NEXT:  .long $tryMap$try_in_catch@IMGREL
; CHECK-NEXT:  .long $ip2state$try_in_catch@IMGREL
; CHECK:       $stateUnwindMap$try_in_catch:
; CHECK-NEXT:  .byte 4
; CHECK-NEXT:  .byte 8
; CHECK-NEXT:  .byte 16
; CHECK:       $tryMap$try_in_catch:
; CHECK-NEXT:  .byte 2
; CHECK-NEXT:  .byte 0
; CHECK-NEXT:  .byte 0
; CHECK-NEXT:  .byte 2
; CHECK-NEXT:  .long $handlerMap$0$try_in_catch@IMGREL
; CHECK:       $handlerMap$0$try_in_catch:
; CHECK-NEXT:  .byte 2
; CHECK-NEXT:  .byte 1
; CHECK-NEXT:  .byte 128
; CHECK-NEXT:  .long "?catch$[[OUTER]]@?0?try_in_catch@4HA"@IMGREL
; CHECK:       "$cppxdata$?catch$[[OUTER]]@?0?try_in_catch@4HA":
; CHECK-NEXT:  .byte 57
; CHECK-NEXT:  .long "$stateUnwindMap$?catch$[[OUTER]]@?0?try_in_catch@4HA"@IMGREL
; CHECK-NEXT:  .long "$tryMap$?catch$[[OUTER]]@?0?try_in_catch@4HA"@IMGREL
; CHECK-NEXT:  .long "$ip2state$?catch$[[OUTER]]@?0?try_in_catch@4HA"@IMGREL
; CHECK-NEXT:  .byte 112
; CHECK:       "$stateUnwindMap$?catch$[[OUTER]]@?0?try_in_catch@4HA":
; CHECK-NEXT:  .byte 6
; CHECK-NEXT:  .byte 8
; CHECK-NEXT:  .byte 8
; CHECK-NEXT:  .byte 16

; Object emission relaxes both IP deltas to one byte. Together with the count
; and state bytes, the following catch FuncInfo starts five bytes after the map.
; OBJ:      Name: $cppxdata$?catch$[[OBJ_OUTER:[0-9]+]]@?0?try_in_catch@4HA
; OBJ-NEXT: Value: [[#%u,CATCH_INFO:]]
; OBJ:      Name: $ip2state$try_in_catch
; OBJ-NEXT: Value: [[#CATCH_INFO-5]]

; CHECK-LABEL: cleanup:
; CHECK:       .seh_handler __CxxFrameHandler4, @unwind{{[[:space:]]*$}}
; CHECK-NOT:   movq $-2
; CHECK:       $cppxdata$cleanup:
; CHECK-NEXT:  .byte 40
; CHECK-NEXT:  .long $stateUnwindMap$cleanup@IMGREL
; CHECK-NEXT:  .long $ip2state$cleanup@IMGREL
; CHECK:       $stateUnwindMap$cleanup:
; CHECK-NEXT:  .byte 2
; CHECK-NEXT:  .byte 14
; CHECK-NEXT:  .long "?dtor${{[0-9]+}}@?0?cleanup@4HA"@IMGREL

; CHECK-LABEL: cannot_throw:
; CHECK:       $cppxdata$cannot_throw:
; CHECK-NEXT:  .byte 104
; CHECK-NEXT:  .long $stateUnwindMap$cannot_throw@IMGREL
; CHECK-NEXT:  .long $ip2state$cannot_throw@IMGREL
