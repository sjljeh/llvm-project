; Every-PC unwind check of NTUW frames from C with SEH: frame shapes, large and
; probed frames, alloca, realignment, nonvolatile FPRs, __try/__except and
; __finally.
; At each reachable instruction the rules must restore the caller exactly,
; and at each return and tail call the state must be the caller's; see
; llvm/utils/ntuw-check.py. Generated from C/C++ with clang -O1.
; RUN: llc -O0 -filetype=obj < %s -o %t.O0.obj
; RUN: %python %llvm_src_root/utils/ntuw-check.py %t.O0.obj | FileCheck %s
; RUN: llc -O2 -filetype=obj < %s -o %t.O2.obj
; RUN: %python %llvm_src_root/utils/ntuw-check.py %t.O2.obj | FileCheck %s
; CHECK: {{.*}}.obj: {{[1-9][0-9]*}} functions, 0 bad

target datalayout = "e-p:64:64-f64:64-n32:64"
target triple = "alpha-pc-windows-msvc19.33.0"

@sink = external dso_local global i32, align 4

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local noundef i32 @leaf(i32 noundef %0, i32 noundef %1) local_unnamed_addr #0 {
  %3 = mul i32 %1, %0
  %4 = add i32 %3, 3
  ret i32 %4
}

; Function Attrs: uwtable
define dso_local noundef i32 @simple_call(i32 noundef %0) local_unnamed_addr #1 {
  tail call void @g(i32 noundef %0)
  %2 = add i32 %0, 1
  ret i32 %2
}

declare dso_local void @g(i32 noundef) local_unnamed_addr #2

; Function Attrs: uwtable
define dso_local i32 @many_nonvol(i32 noundef %0, i32 noundef %1, i32 noundef %2, i32 noundef %3) local_unnamed_addr #1 {
  %5 = tail call i32 @h()
  %6 = tail call i32 @h()
  %7 = tail call i32 @h()
  %8 = tail call i32 @h()
  %9 = tail call i32 @h()
  %10 = tail call i32 @h()
  %11 = tail call i32 @h()
  %12 = add i32 %5, %0
  tail call void @g(i32 noundef %12)
  %13 = add i32 %6, %1
  tail call void @g(i32 noundef %13)
  %14 = add i32 %3, %2
  %15 = add i32 %14, %5
  %16 = add i32 %15, %6
  %17 = add i32 %16, %7
  %18 = add i32 %17, %8
  %19 = add i32 %18, %9
  %20 = add i32 %19, %10
  %21 = add i32 %20, %11
  ret i32 %21
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #3

declare dso_local i32 @h() local_unnamed_addr #2

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #3

; Function Attrs: uwtable
define dso_local double @fp_nonvol(double noundef %0, double noundef %1) local_unnamed_addr #1 {
  %3 = tail call double @gd(double noundef %0)
  %4 = tail call double @gd(double noundef %1)
  %5 = tail call double @gd(double noundef %3)
  %6 = tail call double @llvm.fmuladd.f64(double %3, double %4, double %5)
  %7 = fadd double %0, %6
  %8 = fadd double %1, %7
  ret double %8
}

declare dso_local double @gd(double noundef) local_unnamed_addr #2

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare double @llvm.fmuladd.f64(double, double, double) #4

; Function Attrs: uwtable
define dso_local range(i32 -128, 128) i32 @big_frame(i32 noundef %0) local_unnamed_addr #1 {
  %2 = alloca [40000 x i8], align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #9
  %3 = sext i32 %0 to i64
  %4 = getelementptr inbounds i8, ptr %2, i64 %3
  store volatile i8 1, ptr %4, align 1
  call void @gp(ptr noundef nonnull %2)
  %5 = getelementptr inbounds nuw i8, ptr %2, i64 3
  %6 = load volatile i8, ptr %5, align 1
  %7 = sext i8 %6 to i32
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #9
  ret i32 %7
}

declare dso_local void @gp(ptr noundef) local_unnamed_addr #2

; Function Attrs: uwtable
define dso_local range(i32 -128, 128) i32 @huge_frame(i32 noundef %0) local_unnamed_addr #1 {
  %2 = alloca [200000 x i8], align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #9
  %3 = sext i32 %0 to i64
  %4 = getelementptr inbounds i8, ptr %2, i64 %3
  store volatile i8 1, ptr %4, align 1
  call void @gp(ptr noundef nonnull %2)
  %5 = getelementptr inbounds nuw i8, ptr %2, i64 7
  %6 = load volatile i8, ptr %5, align 1
  %7 = sext i8 %6 to i32
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #9
  ret i32 %7
}

; Function Attrs: uwtable
define dso_local range(i32 -128, 128) i32 @with_alloca(i32 noundef %0) local_unnamed_addr #1 {
  %2 = sext i32 %0 to i64
  %3 = alloca i8, i64 %2, align 8
  store i8 1, ptr %3, align 8
  call void @gp(ptr noundef nonnull %3)
  %4 = add i32 %0, -1
  %5 = sext i32 %4 to i64
  %6 = getelementptr inbounds i8, ptr %3, i64 %5
  %7 = load i8, ptr %6, align 1
  %8 = sext i8 %7 to i32
  ret i32 %8
}

; Function Attrs: uwtable
define dso_local range(i32 -128, 128) i32 @aligned_local(i32 noundef %0) local_unnamed_addr #1 {
  %2 = alloca [64 x i8], align 64
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #9
  %3 = sext i32 %0 to i64
  %4 = getelementptr inbounds i8, ptr %2, i64 %3
  store i8 2, ptr %4, align 1
  call void @gp(ptr noundef nonnull %2)
  %5 = getelementptr inbounds nuw i8, ptr %2, i64 1
  %6 = load i8, ptr %5, align 1
  %7 = sext i8 %6 to i32
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #9
  ret i32 %7
}

; Function Attrs: uwtable
define dso_local noundef i32 @two_returns(i32 noundef %0) local_unnamed_addr #1 {
  %2 = icmp sgt i32 %0, 5
  br i1 %2, label %5, label %3

3:                                                ; preds = %1
  %4 = tail call i32 @h()
  br label %5

5:                                                ; preds = %3, %1
  %6 = phi i32 [ %4, %3 ], [ %0, %1 ]
  %7 = phi i32 [ %4, %3 ], [ 1, %1 ]
  tail call void @g(i32 noundef %6)
  ret i32 %7
}

; Function Attrs: uwtable
define dso_local i32 @page_frame_csr(i32 noundef %0) local_unnamed_addr #1 {
  %2 = alloca [6000 x i8], align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #9
  %3 = tail call i32 @h()
  %4 = tail call i32 @h()
  %5 = tail call i32 @h()
  %6 = sext i32 %0 to i64
  %7 = getelementptr inbounds i8, ptr %2, i64 %6
  store volatile i8 1, ptr %7, align 1
  call void @gp(ptr noundef nonnull %2)
  %8 = getelementptr inbounds nuw i8, ptr %2, i64 3
  %9 = load volatile i8, ptr %8, align 1
  %10 = sext i8 %9 to i32
  %11 = add i32 %4, %3
  %12 = add i32 %11, %5
  %13 = add i32 %12, %10
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #9
  ret i32 %13
}

; Function Attrs: uwtable
define dso_local double @big_frame_csr(i32 noundef %0, double noundef %1) local_unnamed_addr #1 {
  %3 = alloca [40000 x i8], align 1
  call void @llvm.lifetime.start.p0(ptr nonnull %3) #9
  %4 = tail call i32 @h()
  %5 = tail call i32 @h()
  %6 = tail call double @gd(double noundef %1)
  %7 = tail call double @gd(double noundef %6)
  %8 = sext i32 %0 to i64
  %9 = getelementptr inbounds i8, ptr %3, i64 %8
  store volatile i8 1, ptr %9, align 1
  call void @gp(ptr noundef nonnull %3)
  %10 = getelementptr inbounds nuw i8, ptr %3, i64 3
  %11 = load volatile i8, ptr %10, align 1
  %12 = sext i8 %11 to i32
  %13 = add i32 %5, %4
  %14 = add i32 %13, %12
  %15 = sitofp i32 %14 to double
  %16 = fadd double %6, %15
  %17 = fadd double %7, %16
  call void @llvm.lifetime.end.p0(ptr nonnull %3) #9
  ret double %17
}

; Function Attrs: uwtable
define dso_local i32 @alloca_csr(i32 noundef %0) local_unnamed_addr #1 {
  %2 = tail call i32 @h()
  %3 = tail call i32 @h()
  %4 = sext i32 %0 to i64
  %5 = alloca i8, i64 %4, align 8
  store i8 1, ptr %5, align 8
  call void @gp(ptr noundef nonnull %5)
  %6 = add i32 %0, -1
  %7 = sext i32 %6 to i64
  %8 = getelementptr inbounds i8, ptr %5, i64 %7
  %9 = load i8, ptr %8, align 1
  %10 = sext i8 %9 to i32
  %11 = add i32 %3, %2
  %12 = add i32 %11, %10
  ret i32 %12
}

; Function Attrs: uwtable
define dso_local i32 @small_csr(i32 noundef %0) local_unnamed_addr #1 {
  %2 = tail call i32 @h()
  %3 = tail call i32 @h()
  tail call void @g(i32 noundef %0)
  %4 = add i32 %3, %2
  ret i32 %4
}

; Function Attrs: uwtable
define dso_local i32 @seh_filter(ptr noundef %0, i32 noundef %1) #1 personality ptr @__C_specific_handler {
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  call void (...) @llvm.localescape(ptr nonnull %4)
  store ptr %0, ptr %3, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %4) #9
  %5 = add i32 %1, 7
  store i32 %5, ptr %4, align 4
  invoke void @llvm.seh.try.begin()
          to label %6 unwind label %12

6:                                                ; preds = %2
  %7 = load volatile ptr, ptr %3, align 8
  %8 = load volatile i32, ptr %7, align 4
  %9 = load volatile i32, ptr %4, align 4
  %10 = add i32 %9, %8
  store volatile i32 %10, ptr %4, align 4
  invoke void @may_fault() #10
          to label %11 unwind label %12

11:                                               ; preds = %6
  invoke void @llvm.seh.try.end()
          to label %20 unwind label %12

12:                                               ; preds = %11, %6, %2
  %13 = catchswitch within none [label %14] unwind to caller

14:                                               ; preds = %12
  %15 = catchpad within %13 [ptr @"?filt$0@0@seh_filter@@"]
  catchret from %15 to label %16

16:                                               ; preds = %14
  %17 = call i32 @llvm.eh.exceptioncode(token %15)
  %18 = load i32, ptr %4, align 4
  %19 = sub i32 0, %18
  store i32 %19, ptr %4, align 4
  br label %20

20:                                               ; preds = %16, %11
  %21 = load i32, ptr %4, align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %4) #9
  ret i32 %21
}

; Function Attrs: uwtable
define internal i32 @"?filt$0@0@seh_filter@@"(ptr nofree noundef readonly captures(none) %0, ptr nofree noundef readonly %1) #1 {
  %3 = tail call ptr @llvm.eh.recoverfp(ptr nonnull @seh_filter, ptr %1)
  %4 = tail call ptr @llvm.localrecover(ptr nonnull @seh_filter, ptr %3, i32 0)
  %5 = load ptr, ptr %0, align 8
  %6 = load i32, ptr %5, align 4
  %7 = load i32, ptr %4, align 4
  %8 = tail call i32 @inspect_exception(i32 noundef %6, i32 noundef %7)
  ret i32 %8
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare ptr @llvm.eh.recoverfp(ptr, ptr) #5

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg) #5

declare dso_local i32 @inspect_exception(i32 noundef, i32 noundef) local_unnamed_addr #2

; Function Attrs: nounwind willreturn memory(write)
declare void @llvm.seh.try.begin() #6

declare dso_local i32 @__C_specific_handler(...)

declare dso_local void @may_fault() local_unnamed_addr #2

; Function Attrs: nounwind willreturn memory(write)
declare void @llvm.seh.try.end() #6

; Function Attrs: nounwind memory(none)
declare i32 @llvm.eh.exceptioncode(token) #7

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.localescape(...) #8

; Function Attrs: uwtable
define dso_local void @seh_finally(i32 noundef %0) #1 personality ptr @__C_specific_handler {
  %2 = alloca i32, align 4
  call void (...) @llvm.localescape(ptr nonnull %2)
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #9
  %3 = add i32 %0, 3
  store i32 %3, ptr %2, align 4
  invoke void @llvm.seh.try.begin()
          to label %4 unwind label %10

4:                                                ; preds = %1
  invoke void @may_fault() #10
          to label %5 unwind label %10

5:                                                ; preds = %4
  invoke void @llvm.seh.try.end()
          to label %6 unwind label %10

6:                                                ; preds = %5
  %7 = tail call ptr @llvm.localaddress()
  %8 = call ptr @llvm.localrecover(ptr nonnull @seh_finally, ptr readonly %7, i32 0)
  %9 = load i32, ptr %8, align 4
  store volatile i32 %9, ptr @sink, align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #9
  ret void

10:                                               ; preds = %5, %4, %1
  %11 = cleanuppad within none []
  %12 = tail call ptr @llvm.localaddress()
  %13 = call ptr @llvm.localrecover(ptr nonnull @seh_finally, ptr readonly %12, i32 0)
  %14 = load i32, ptr %13, align 4
  %15 = add i32 %14, 1
  store volatile i32 %15, ptr @sink, align 4
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #9
  cleanupret from %11 unwind to caller
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare ptr @llvm.localaddress() #5

; Function Attrs: uwtable
define dso_local i32 @seh_code() local_unnamed_addr #1 personality ptr @__C_specific_handler {
  invoke void @llvm.seh.try.begin()
          to label %1 unwind label %3

1:                                                ; preds = %0
  invoke void @may_fault() #10
          to label %2 unwind label %3

2:                                                ; preds = %1
  invoke void @llvm.seh.try.end()
          to label %9 unwind label %3

3:                                                ; preds = %2, %1, %0
  %4 = catchswitch within none [label %5] unwind to caller

5:                                                ; preds = %3
  %6 = catchpad within %4 [ptr null]
  catchret from %6 to label %7

7:                                                ; preds = %5
  %8 = tail call i32 @llvm.eh.exceptioncode(token %6)
  br label %9

9:                                                ; preds = %7, %2
  %10 = phi i32 [ %8, %7 ], [ 0, %2 ]
  ret i32 %10
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="8" }
attributes #1 = { uwtable "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="8" }
attributes #2 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #4 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #5 = { nocallback nofree nosync nounwind willreturn memory(none) }
attributes #6 = { nounwind willreturn memory(write) }
attributes #7 = { nounwind memory(none) }
attributes #8 = { nocallback nofree nosync nounwind willreturn }
attributes #9 = { nounwind }
attributes #10 = { noinline }

!llvm.module.flags = !{!1, !2}

!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 7, !"uwtable", i32 2}
