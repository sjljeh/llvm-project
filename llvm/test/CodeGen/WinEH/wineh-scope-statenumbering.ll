; RUN: opt -mtriple=i386-pc-windows-msvc -S -x86-winehstate < %s | FileCheck %s
; RUN: opt -mtriple=i386-pc-windows-msvc -S -passes=x86-winehstate < %s | FileCheck %s

target datalayout = "e-m:x-p:32:32-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32-a:0:32-S32"
target triple = "i386-pc-windows-msvc19.42.34433"

%struct.Destructor = type { ptr }

define dso_local void @"?HandleDestructorCallWithException@@YAXPA_N@Z"(ptr noundef %destructorCalled) personality ptr @__CxxFrameHandler3 {
entry:
  %destructorCalled.addr = alloca ptr, align 4
  %x = alloca %struct.Destructor, align 4
  store ptr %destructorCalled, ptr %destructorCalled.addr, align 4
  %0 = load ptr, ptr %destructorCalled.addr, align 4
  %call = call x86_thiscallcc noundef ptr @"??0Destructor@@QAE@PA_N@Z"(ptr noundef nonnull align 4 dereferenceable(4) %x, ptr noundef %0)
  ; CHECK:  store i32 0, ptr %9, align 4
  ; CHECK-NEXT:  invoke void @llvm.seh.scope.begin()
  invoke void @llvm.seh.scope.begin()
          to label %invoke.cont unwind label %ehcleanup

invoke.cont:
  store i32 1, ptr inttoptr (i32 1 to ptr), align 4
  ; CHECK:  store i32 -1, ptr %10, align 4
  ; CHECK-NEXT:  invoke void @llvm.seh.scope.end()
  invoke void @llvm.seh.scope.end()
          to label %invoke.cont1 unwind label %ehcleanup

invoke.cont1:
  call x86_thiscallcc void @"??1Destructor@@QAE@XZ"(ptr noundef nonnull align 4 dereferenceable(4) %x) #1
  ret void

ehcleanup:
  %1 = cleanuppad within none []
  call x86_thiscallcc void @"??1Destructor@@QAE@XZ"(ptr noundef nonnull align 4 dereferenceable(4) %x) #1 [ "funclet"(token %1) ]
  cleanupret from %1 unwind to caller
}

define void @copy_in_try(ptr %dst, ptr %src, i32 %size) personality ptr @_except_handler3 {
entry:
  ; CHECK-LABEL: define void @copy_in_try(
  ; CHECK: store i32 0
  ; CHECK-NEXT: invoke void @llvm.seh.try.begin()
  invoke void @llvm.seh.try.begin()
          to label %try unwind label %catch.dispatch

try:
  ; CHECK: try:
  ; CHECK-NOT: store i32 -1
  ; CHECK: call void @llvm.memcpy.p0.p0.i32
  call void @llvm.memcpy.p0.p0.i32(ptr align 1 %dst, ptr align 1 %src,
                                   i32 %size, i1 true)
  ; CHECK: store i32 -1
  ; CHECK-NEXT: invoke void @llvm.seh.try.end()
  invoke void @llvm.seh.try.end()
          to label %done unwind label %catch.dispatch

done:
  ret void

catch.dispatch:
  %cs = catchswitch within none [label %catch] unwind to caller

catch:
  %cp = catchpad within %cs [ptr @filter]
  catchret from %cp to label %done
}

define internal i32 @filter() {
  ret i32 1
}

declare dso_local i32 @__CxxFrameHandler3(...)
declare i32 @_except_handler3(...)
declare dso_local void @llvm.seh.scope.begin() #0
declare dso_local void @llvm.seh.scope.end() #0
declare void @llvm.seh.try.begin() #0
declare void @llvm.seh.try.end() #0
declare void @llvm.memcpy.p0.p0.i32(ptr noalias nocapture writeonly,
                                    ptr noalias nocapture readonly, i32,
                                    i1 immarg) #1

declare dso_local x86_thiscallcc noundef ptr @"??0Destructor@@QAE@PA_N@Z"(ptr noundef nonnull returned align 4 dereferenceable(4) %this, ptr noundef %destructorCalled)
declare dso_local x86_thiscallcc void @"??1Destructor@@QAE@XZ"(ptr noundef nonnull align 4 dereferenceable(4) %this) #1

attributes #0 = { nounwind memory(none) }
attributes #1 = { nounwind }
