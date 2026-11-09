; /GS under NTUW: the stack protector uses the MSVC CRT's __security_cookie
; and __security_check_cookie, and the language handler checks the cookie of a
; frame that is being unwound: __GSHandlerCheck for a function with no other
; handler, __GSHandlerCheck_SEH after a C scope table. The handler data ends
; with the cookie slot's offset from the establisher frame and flags.
; Generated from C with clang -O2 -fstack-protector-strong.
; RUN: llc -O2 < %s | FileCheck %s
; RUN: llc -O2 -filetype=obj < %s -o %t.obj
; RUN: %python %llvm_src_root/utils/ntuw-check.py %t.obj | FileCheck %s --check-prefix=EVERYPC

; CHECK-LABEL: plain:
; CHECK:         .seh_handler __GSHandlerCheck, @unwind, @except
; CHECK:         lda $11,64($30)
; CHECK:         bsr $26,$__security_check_cookie..ng
; CHECK:         .seh_handlerdata
; CHECK-NEXT:    .long -48 # GS cookie offset
; CHECK-NEXT:    .long 0 # GS flags
; CHECK-LABEL: seh:
; CHECK:         .seh_handler __GSHandlerCheck_SEH, @unwind, @except
; CHECK:         .seh_handlerdata
; CHECK:         Number of call sites
; CHECK:         .long -48 # GS cookie offset
; CHECK-NEXT:    .long 0 # GS flags

; EVERYPC: 3 functions, 0 bad

target datalayout = "e-p:32:32-f64:64-n32:64"
target triple = "alpha-pc-windows-msvc19.33.0"

; Function Attrs: nounwind sspstrong uwtable
define dso_local range(i32 -128, 128) i32 @plain(i32 noundef %0) local_unnamed_addr #0 {
  %2 = alloca [64 x i8], align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #7
  call void @use(ptr noundef nonnull %2) #7
  %3 = getelementptr inbounds i8, ptr %2, i32 %0
  %4 = load i8, ptr %3, align 1
  %5 = sext i8 %4 to i32
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #7
  ret i32 %5
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

declare dso_local void @use(ptr noundef) local_unnamed_addr #2

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

; Function Attrs: nounwind sspstrong uwtable
define dso_local range(i32 -128, 128) i32 @seh(i32 noundef %0) #0 personality ptr @__C_specific_handler {
  %2 = alloca i32, align 4
  %3 = alloca [64 x i8], align 1
  call void (...) @llvm.localescape(ptr nonnull %2)
  store i32 %0, ptr %2, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %3) #7
  invoke void @llvm.seh.try.begin()
          to label %4 unwind label %6

4:                                                ; preds = %1
  invoke void @use(ptr noundef nonnull %3) #8
          to label %5 unwind label %6

5:                                                ; preds = %4
  invoke void @llvm.seh.try.end()
          to label %12 unwind label %6

6:                                                ; preds = %5, %4, %1
  %7 = catchswitch within none [label %8] unwind to caller

8:                                                ; preds = %6
  %9 = catchpad within %7 [ptr @"?filt$0@0@seh@@"]
  catchret from %9 to label %10

10:                                               ; preds = %8
  %11 = call i32 @llvm.eh.exceptioncode(token %9)
  br label %17

12:                                               ; preds = %5
  %13 = load i32, ptr %2, align 4
  %14 = getelementptr inbounds i8, ptr %3, i32 %13
  %15 = load i8, ptr %14, align 1
  %16 = sext i8 %15 to i32
  br label %17

17:                                               ; preds = %12, %10
  %18 = phi i32 [ %16, %12 ], [ -1, %10 ]
  call void @llvm.lifetime.end.p0(ptr nonnull %3) #7
  ret i32 %18
}

; Function Attrs: nounwind sspstrong uwtable
define internal i32 @"?filt$0@0@seh@@"(ptr nofree readnone captures(none) %0, ptr nofree noundef readonly %1) #0 {
  %3 = tail call ptr @llvm.eh.recoverfp(ptr nonnull @seh, ptr %1)
  %4 = tail call ptr @llvm.localrecover(ptr nonnull @seh, ptr %3, i32 0)
  %5 = load i32, ptr %4, align 4
  %6 = tail call i32 @may(i32 noundef %5) #7
  ret i32 %6
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare ptr @llvm.eh.recoverfp(ptr, ptr) #3

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg) #3

declare dso_local i32 @may(i32 noundef) local_unnamed_addr #2

; Function Attrs: nounwind willreturn memory(write)
declare void @llvm.seh.try.begin() #4

declare dso_local i32 @__C_specific_handler(...)

; Function Attrs: nounwind willreturn memory(write)
declare void @llvm.seh.try.end() #4

; Function Attrs: nounwind memory(none)
declare i32 @llvm.eh.exceptioncode(token) #5

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.localescape(...) #6

attributes #0 = { nounwind sspstrong uwtable "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="8" "target-features"="+taso" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-features"="+taso" }
attributes #3 = { nocallback nofree nosync nounwind willreturn memory(none) }
attributes #4 = { nounwind willreturn memory(write) }
attributes #5 = { nounwind memory(none) }
attributes #6 = { nocallback nofree nosync nounwind willreturn }
attributes #7 = { nounwind }
attributes #8 = { noinline }

!llvm.module.flags = !{!1, !2, !3}

!1 = !{i32 1, !"target-abi", !"taso"}
!2 = !{i32 4, !"probe-stack", !"inline-asm"}
!3 = !{i32 7, !"uwtable", i32 2}
