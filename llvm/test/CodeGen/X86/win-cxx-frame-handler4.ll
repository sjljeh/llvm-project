; RUN: llc -verify-machineinstrs -mtriple=x86_64-pc-windows-msvc < %s | FileCheck %s
; RUN: llc -verify-machineinstrs -mtriple=x86_64-pc-windows-msvc -filetype=obj < %s -o %t
; RUN: llvm-readobj --symbols %t | FileCheck %s --check-prefix=OBJ

declare void @f(i32)
declare void @destroy(ptr) nounwind
declare void @terminate() nounwind
declare void @llvm.lifetime.start.p0(i64 immarg, ptr nocapture) nounwind
declare void @llvm.lifetime.end.p0(i64 immarg, ptr nocapture) nounwind
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

define void @cleanup_pointer(ptr %object) personality ptr @__CxxFrameHandler4 {
entry:
  %slot = alloca ptr
  store ptr %object, ptr %slot
  invoke void @f(i32 5)
          to label %return unwind label %cleanup.pad

return:
  call void @destroy(ptr %object)
  ret void

cleanup.pad:
  %cleanup = cleanuppad within none []
  %loaded = load ptr, ptr %slot
  call void @destroy(ptr %loaded) [ "funclet"(token %cleanup) ]
  cleanupret from %cleanup unwind to caller
}

define void @branched_cleanup() personality ptr @__CxxFrameHandler4 {
entry:
  %object = alloca i8
  invoke void @f(i32 6)
          to label %return unwind label %cleanup.pad

return:
  call void @destroy(ptr %object)
  ret void

cleanup.pad:
  %cleanup = cleanuppad within none []
  call void @destroy(ptr %object) [ "funclet"(token %cleanup) ]
  br label %cleanup.more

cleanup.more:
  call void @destroy(ptr %object) [ "funclet"(token %cleanup) ]
  cleanupret from %cleanup unwind to caller
}

define void @nested_direct_cleanup() personality ptr @__CxxFrameHandler4 {
entry:
  %outer = alloca i8
  %inner = alloca i8
  call void @llvm.lifetime.start.p0(i64 1, ptr %outer)
  call void @llvm.lifetime.start.p0(i64 1, ptr %inner)
  invoke void @f(i32 7)
          to label %inner.done unwind label %inner.dispatch

inner.dispatch:
  %inner.cs = catchswitch within none [label %inner.catch]
              unwind label %inner.cleanup

inner.catch:
  %inner.cp = catchpad within %inner.cs [ptr null, i32 64, ptr null]
  catchret from %inner.cp to label %inner.done

inner.done:
  call void @destroy(ptr %inner)
  call void @llvm.lifetime.end.p0(i64 1, ptr %inner)
  br label %return

inner.cleanup:
  %inner.cl = cleanuppad within none []
  call void @destroy(ptr %inner) [ "funclet"(token %inner.cl) ]
  call void @llvm.lifetime.end.p0(i64 1, ptr %inner) [ "funclet"(token %inner.cl) ]
  cleanupret from %inner.cl unwind label %outer.dispatch

outer.dispatch:
  %outer.cs = catchswitch within none [label %outer.catch]
              unwind label %outer.cleanup

outer.catch:
  %outer.cp = catchpad within %outer.cs [ptr null, i32 64, ptr null]
  catchret from %outer.cp to label %return

return:
  call void @destroy(ptr %outer)
  call void @llvm.lifetime.end.p0(i64 1, ptr %outer)
  ret void

outer.cleanup:
  %outer.cl = cleanuppad within none []
  call void @destroy(ptr %outer) [ "funclet"(token %outer.cl) ]
  call void @llvm.lifetime.end.p0(i64 1, ptr %outer) [ "funclet"(token %outer.cl) ]
  cleanupret from %outer.cl unwind to caller
}

define void @cannot_throw() nounwind personality ptr @__CxxFrameHandler4 {
entry:
  invoke void @f(i32 8)
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
; OBJ-NOT:  Name: ?catch$-1

; CHECK-LABEL: cleanup:
; CHECK:       .seh_handler __CxxFrameHandler4, @unwind{{[[:space:]]*$}}
; CHECK-NOT:   movq $-2
; CHECK-NOT:   "?dtor$
; CHECK:       $cppxdata$cleanup:
; CHECK-NEXT:  .byte 40
; CHECK-NEXT:  .long $stateUnwindMap$cleanup@IMGREL
; CHECK-NEXT:  .long $ip2state$cleanup@IMGREL
; CHECK:       $stateUnwindMap$cleanup:
; CHECK-NEXT:  .byte 2
; CHECK-NEXT:  .byte 10
; CHECK-NEXT:  .long destroy@IMGREL
; CHECK-NEXT:  .byte {{[0-9]+}}

; CHECK-LABEL: cleanup_pointer:
; CHECK-NOT:   "?dtor$
; CHECK:       $stateUnwindMap$cleanup_pointer:
; CHECK-NEXT:  .byte 2
; CHECK-NEXT:  .byte 12
; CHECK-NEXT:  .long destroy@IMGREL
; CHECK-NEXT:  .byte {{[0-9]+}}

; CHECK-LABEL: branched_cleanup:
; CHECK:       "?dtor$[[BRANCHED:[0-9]+]]@?0?branched_cleanup@4HA":
; CHECK:       $stateUnwindMap$branched_cleanup:
; CHECK-NEXT:  .byte 2
; CHECK-NEXT:  .byte 14
; CHECK-NEXT:  .long "?dtor$[[BRANCHED]]@?0?branched_cleanup@4HA"@IMGREL

; Removing the direct inner cleanup must retain the outer catch's EH edge. The
; two simultaneously-live action objects must also keep distinct stack slots.
; CHECK-LABEL: nested_direct_cleanup:
; CHECK-NOT:   "?dtor$
; CHECK:       $stateUnwindMap$nested_direct_cleanup:
; CHECK:       .long destroy@IMGREL
; CHECK-NEXT:  .byte 92
; CHECK:       .long destroy@IMGREL
; CHECK-NEXT:  .byte 94

; CHECK-LABEL: cannot_throw:
; CHECK:       $cppxdata$cannot_throw:
; CHECK-NEXT:  .byte 104
; CHECK-NEXT:  .long $stateUnwindMap$cannot_throw@IMGREL
; CHECK-NEXT:  .long $ip2state$cannot_throw@IMGREL
