; Every-PC unwind check of NTUW frames from C++ EH: catch funclets, cleanups,
; rethrow and nested try blocks.
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

%rtti.TypeDescriptor2 = type { ptr, ptr, [3 x i8] }
%rtti.TypeDescriptor7 = type { ptr, ptr, [8 x i8] }
%rtti.TypeDescriptor5 = type { ptr, ptr, [6 x i8] }
%eh.CatchableType = type { i32, i32, i32, i32, i32, i32, i32 }
%eh.CatchableTypeArray.1 = type { i32, [1 x i32] }
%eh.ThrowInfo = type { i32, i32, i32, i32 }
%struct.D = type { i32 }
%struct.E = type { i32, [24 x i8] }

$"??_R0J@8" = comdat any

$"??_R0H@8" = comdat any

$"??_R0?AUE@@@8" = comdat any

$"??_R0PEAH@8" = comdat any

$"_CT??_R0H@84" = comdat any

$_CTA1H = comdat any

$_TI1H = comdat any

$"_CT??_R0?AUE@@@8??0E@@QEAA@AEBU0@@Z28" = comdat any

$"_CTA1?AUE@@" = comdat any

$"_TI1?AUE@@" = comdat any

@"??_7type_info@@6B@" = external constant ptr
@"??_R0J@8" = linkonce_odr global %rtti.TypeDescriptor2 { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".J\00" }, comdat
@"??_R0H@8" = linkonce_odr global %rtti.TypeDescriptor2 { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".H\00" }, comdat
@"??_R0?AUE@@@8" = linkonce_odr global %rtti.TypeDescriptor7 { ptr @"??_7type_info@@6B@", ptr null, [8 x i8] c".?AUE@@\00" }, comdat
@"??_R0PEAH@8" = linkonce_odr global %rtti.TypeDescriptor5 { ptr @"??_7type_info@@6B@", ptr null, [6 x i8] c".PEAH\00" }, comdat
@__ImageBase = external dso_local constant i8
@"_CT??_R0H@84" = linkonce_odr unnamed_addr constant %eh.CatchableType { i32 1, i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @"??_R0H@8" to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32), i32 0, i32 -1, i32 0, i32 4, i32 0 }, section ".xdata", comdat
@_CTA1H = linkonce_odr unnamed_addr constant %eh.CatchableTypeArray.1 { i32 1, [1 x i32] [i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @"_CT??_R0H@84" to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32)] }, section ".xdata", comdat
@_TI1H = linkonce_odr unnamed_addr constant %eh.ThrowInfo { i32 0, i32 0, i32 0, i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @_CTA1H to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32) }, section ".xdata", comdat
@"_CT??_R0?AUE@@@8??0E@@QEAA@AEBU0@@Z28" = linkonce_odr unnamed_addr constant %eh.CatchableType { i32 0, i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @"??_R0?AUE@@@8" to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32), i32 0, i32 -1, i32 0, i32 28, i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @"??0E@@QEAA@AEBU0@@Z" to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32) }, section ".xdata", comdat
@"_CTA1?AUE@@" = linkonce_odr unnamed_addr constant %eh.CatchableTypeArray.1 { i32 1, [1 x i32] [i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @"_CT??_R0?AUE@@@8??0E@@QEAA@AEBU0@@Z28" to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32)] }, section ".xdata", comdat
@"_TI1?AUE@@" = linkonce_odr unnamed_addr constant %eh.ThrowInfo { i32 0, i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @"??1E@@QEAA@XZ" to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32), i32 0, i32 trunc (i64 sub nuw nsw (i64 ptrtoint (ptr @"_CTA1?AUE@@" to i64), i64 ptrtoint (ptr @__ImageBase to i64)) to i32) }, section ".xdata", comdat

; Function Attrs: mustprogress uwtable
define dso_local noundef range(i32 0, 2) i32 @"?try_catch_all@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 1)
          to label %5 unwind label %1

1:                                                ; preds = %0
  %2 = catchswitch within none [label %3] unwind to caller

3:                                                ; preds = %1
  %4 = catchpad within %2 [ptr null, i32 64, ptr null]
  catchret from %4 to label %5

5:                                                ; preds = %3, %0
  %6 = phi i32 [ 1, %3 ], [ 0, %0 ]
  ret i32 %6
}

declare dso_local void @"?may_throw@@YAXH@Z"(i32 noundef) local_unnamed_addr #1

declare dso_local i32 @__CxxFrameHandler3(...)

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?nested_try@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca i32, align 4
  %2 = alloca i32, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 1)
          to label %3 unwind label %12

3:                                                ; preds = %0
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 2)
          to label %10 unwind label %4

4:                                                ; preds = %3
  %5 = catchswitch within none [label %6] unwind label %12

6:                                                ; preds = %4
  %7 = catchpad within %5 [ptr @"??_R0H@8", i32 0, ptr %1]
  %8 = load i32, ptr %1, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 3) [ "funclet"(token %7) ]
          to label %9 unwind label %12

9:                                                ; preds = %6
  catchret from %7 to label %10

10:                                               ; preds = %9, %3
  %11 = phi i32 [ 0, %3 ], [ %8, %9 ]
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 4)
          to label %19 unwind label %12

12:                                               ; preds = %10, %6, %4, %0
  %13 = phi i32 [ %11, %10 ], [ 0, %4 ], [ %8, %6 ], [ 0, %0 ]
  %14 = catchswitch within none [label %15, label %21] unwind to caller

15:                                               ; preds = %12
  %16 = catchpad within %14 [ptr @"??_R0J@8", i32 0, ptr %2]
  %17 = load i32, ptr %2, align 4
  %18 = add i32 %17, %13
  catchret from %16 to label %19

19:                                               ; preds = %21, %15, %10
  %20 = phi i32 [ %11, %10 ], [ %18, %15 ], [ %23, %21 ]
  ret i32 %20

21:                                               ; preds = %12
  %22 = catchpad within %14 [ptr null, i32 64, ptr null]
  %23 = add i32 %13, -1
  catchret from %22 to label %19
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #2

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #2

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?dtors@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca %struct.D, align 4
  %2 = alloca %struct.D, align 4
  %3 = alloca %struct.D, align 4
  %4 = alloca %struct.D, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %1) #5
  %5 = call noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %1, i32 noundef 1)
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #5
  %6 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %2, i32 noundef 2)
          to label %7 unwind label %28

7:                                                ; preds = %0
  %8 = invoke noundef i32 @"?get@@YAHXZ"()
          to label %9 unwind label %24

9:                                                ; preds = %7
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %8)
          to label %10 unwind label %24

10:                                               ; preds = %9
  call void @llvm.lifetime.start.p0(ptr nonnull %3) #5
  %11 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %3, i32 noundef 3)
          to label %12 unwind label %24

12:                                               ; preds = %10
  %13 = load i32, ptr %3, align 4
  %14 = load i32, ptr %2, align 4
  %15 = add i32 %14, %13
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %15)
          to label %16 unwind label %22

16:                                               ; preds = %12
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %3) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %3) #5
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #5
  call void @llvm.lifetime.start.p0(ptr nonnull %4) #5
  %17 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %4, i32 noundef 4)
          to label %18 unwind label %28

18:                                               ; preds = %16
  %19 = load i32, ptr %4, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %19)
          to label %20 unwind label %26

20:                                               ; preds = %18
  %21 = load i32, ptr %1, align 4
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %4) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %4) #5
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %1) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %1) #5
  ret i32 %21

22:                                               ; preds = %12
  %23 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %3) #5 [ "funclet"(token %23) ]
  cleanupret from %23 unwind label %24

24:                                               ; preds = %22, %10, %9, %7
  %25 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5 [ "funclet"(token %25) ]
  cleanupret from %25 unwind label %28

26:                                               ; preds = %18
  %27 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %4) #5 [ "funclet"(token %27) ]
  cleanupret from %27 unwind label %28

28:                                               ; preds = %26, %24, %16, %0
  %29 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %1) #5 [ "funclet"(token %29) ]
  call void @llvm.lifetime.end.p0(ptr nonnull %1) #5
  cleanupret from %29 unwind to caller
}

declare dso_local noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull returned align 4 dereferenceable(4), i32 noundef) unnamed_addr #1

declare dso_local noundef i32 @"?get@@YAHXZ"() local_unnamed_addr #1

; Function Attrs: nounwind
declare dso_local void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4)) unnamed_addr #3

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?dtor_in_try@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca %struct.D, align 4
  %2 = alloca %struct.D, align 4
  %3 = alloca ptr, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %1) #5
  %4 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %1, i32 noundef 1)
          to label %5 unwind label %16

5:                                                ; preds = %0
  %6 = load i32, ptr %1, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %6)
          to label %7 unwind label %14

7:                                                ; preds = %5
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #5
  %8 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %2, i32 noundef 2)
          to label %9 unwind label %14

9:                                                ; preds = %7
  %10 = load i32, ptr %2, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %10)
          to label %11 unwind label %12

11:                                               ; preds = %9
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #5
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %1) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %1) #5
  br label %22

12:                                               ; preds = %9
  %13 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5 [ "funclet"(token %13) ]
  cleanupret from %13 unwind label %14

14:                                               ; preds = %12, %7, %5
  %15 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %1) #5 [ "funclet"(token %15) ]
  cleanupret from %15 unwind label %16

16:                                               ; preds = %14, %0
  %17 = catchswitch within none [label %18] unwind to caller

18:                                               ; preds = %16
  %19 = catchpad within %17 [ptr @"??_R0?AUE@@@8", i32 8, ptr %3]
  %20 = load ptr, ptr %3, align 8, !nonnull !3, !align !4
  %21 = load i32, ptr %20, align 4
  catchret from %19 to label %22

22:                                               ; preds = %18, %11
  %23 = phi i32 [ 0, %11 ], [ %21, %18 ]
  ret i32 %23
}

; Function Attrs: mustprogress uwtable
define dso_local void @"?rethrow@@YAXXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca ptr, align 8
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 5)
          to label %9 unwind label %2

2:                                                ; preds = %0
  %3 = catchswitch within none [label %4] unwind to caller

4:                                                ; preds = %2
  %5 = catchpad within %3 [ptr @"??_R0?AUE@@@8", i32 8, ptr %1]
  %6 = load ptr, ptr %1, align 8, !nonnull !3, !align !4
  %7 = load i32, ptr %6, align 4
  %8 = add i32 %7, 1
  store i32 %8, ptr %6, align 4
  call void @_CxxThrowException(ptr null, ptr null) #6 [ "funclet"(token %5) ]
  unreachable

9:                                                ; preds = %0
  ret void
}

declare dso_local void @_CxxThrowException(ptr, ptr) local_unnamed_addr

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?catch_by_value@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca ptr, align 8
  %2 = alloca %struct.E, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 6)
          to label %15 unwind label %3

3:                                                ; preds = %0
  %4 = catchswitch within none [label %5, label %9] unwind to caller

5:                                                ; preds = %3
  %6 = catchpad within %4 [ptr @"??_R0?AUE@@@8", i32 0, ptr %2]
  invoke void @"?use@@YAXPEAX@Z"(ptr noundef nonnull %2) [ "funclet"(token %6) ]
          to label %7 unwind label %13

7:                                                ; preds = %5
  %8 = load i32, ptr %2, align 4
  call void @"??1E@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(28) dereferenceable(28) %2) #5 [ "funclet"(token %6) ]
  catchret from %6 to label %15

9:                                                ; preds = %3
  %10 = catchpad within %4 [ptr @"??_R0PEAH@8", i32 0, ptr %1]
  %11 = load ptr, ptr %1, align 8
  %12 = load i32, ptr %11, align 4
  catchret from %10 to label %15

13:                                               ; preds = %5
  %14 = cleanuppad within %6 []
  call void @"??1E@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(28) dereferenceable(28) %2) #5 [ "funclet"(token %14) ]
  cleanupret from %14 unwind to caller

15:                                               ; preds = %9, %7, %0
  %16 = phi i32 [ %12, %9 ], [ %8, %7 ], [ 0, %0 ]
  ret i32 %16
}

declare dso_local void @"?use@@YAXPEAX@Z"(ptr noundef) local_unnamed_addr #1

; Function Attrs: nounwind
declare dso_local void @"??1E@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(28) dereferenceable(28)) unnamed_addr #3

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?catch_local_dtor@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca i32, align 4
  %2 = alloca %struct.D, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 7)
          to label %14 unwind label %3

3:                                                ; preds = %0
  %4 = catchswitch within none [label %5] unwind to caller

5:                                                ; preds = %3
  %6 = catchpad within %4 [ptr @"??_R0H@8", i32 0, ptr %1]
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #5
  %7 = load i32, ptr %1, align 4
  %8 = call noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %2, i32 noundef %7) [ "funclet"(token %6) ]
  %9 = load i32, ptr %2, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %9) [ "funclet"(token %6) ]
          to label %10 unwind label %12

10:                                               ; preds = %5
  %11 = load i32, ptr %2, align 4
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5 [ "funclet"(token %6) ]
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #5
  catchret from %6 to label %14

12:                                               ; preds = %5
  %13 = cleanuppad within %6 []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5 [ "funclet"(token %13) ]
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #5
  cleanupret from %13 unwind to caller

14:                                               ; preds = %10, %0
  %15 = phi i32 [ %11, %10 ], [ 0, %0 ]
  ret i32 %15
}

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?throw_from_catch@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca %struct.D, align 4
  %2 = alloca %struct.D, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  call void @llvm.lifetime.start.p0(ptr nonnull %1) #5
  %5 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %1, i32 noundef 1)
          to label %6 unwind label %18

6:                                                ; preds = %0
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef 8)
          to label %15 unwind label %7

7:                                                ; preds = %6
  %8 = catchswitch within none [label %9] unwind label %16

9:                                                ; preds = %7
  %10 = catchpad within %8 [ptr @"??_R0H@8", i32 0, ptr null]
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #5
  %11 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %2, i32 noundef 2) [ "funclet"(token %10) ]
          to label %12 unwind label %16

12:                                               ; preds = %9
  store i32 5, ptr %3, align 4
  invoke void @_CxxThrowException(ptr nonnull %3, ptr nonnull @_TI1H) #6 [ "funclet"(token %10) ]
          to label %25 unwind label %13

13:                                               ; preds = %12
  %14 = cleanuppad within %10 []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5 [ "funclet"(token %14) ]
  cleanupret from %14 unwind label %16

15:                                               ; preds = %6
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %1) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %1) #5
  br label %23

16:                                               ; preds = %13, %9, %7
  %17 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %1) #5 [ "funclet"(token %17) ]
  cleanupret from %17 unwind label %18

18:                                               ; preds = %16, %0
  %19 = catchswitch within none [label %20] unwind to caller

20:                                               ; preds = %18
  %21 = catchpad within %19 [ptr @"??_R0H@8", i32 0, ptr %4]
  %22 = load i32, ptr %4, align 4
  catchret from %21 to label %23

23:                                               ; preds = %20, %15
  %24 = phi i32 [ 0, %15 ], [ %22, %20 ]
  ret i32 %24

25:                                               ; preds = %12
  unreachable
}

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?big_frame@@YAHXZ"() local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %1 = alloca [40000 x i8], align 1
  %2 = alloca %struct.D, align 4
  %3 = alloca ptr, align 8
  call void @llvm.lifetime.start.p0(ptr nonnull %1) #5
  call void @"?use@@YAXPEAX@Z"(ptr noundef nonnull %1)
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #5
  %4 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %2, i32 noundef 1)
          to label %5 unwind label %10

5:                                                ; preds = %0
  %6 = load i32, ptr %2, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %6)
          to label %7 unwind label %8

7:                                                ; preds = %5
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #5
  br label %16

8:                                                ; preds = %5
  %9 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5 [ "funclet"(token %9) ]
  cleanupret from %9 unwind label %10

10:                                               ; preds = %8, %0
  %11 = catchswitch within none [label %12] unwind to caller

12:                                               ; preds = %10
  %13 = catchpad within %11 [ptr @"??_R0?AUE@@@8", i32 8, ptr %3]
  call void @"?use@@YAXPEAX@Z"(ptr noundef nonnull %1) [ "funclet"(token %13) ]
  %14 = load ptr, ptr %3, align 8, !nonnull !3, !align !4
  %15 = load i32, ptr %14, align 4
  catchret from %13 to label %16

16:                                               ; preds = %12, %7
  %17 = phi i32 [ 0, %7 ], [ %15, %12 ]
  call void @llvm.lifetime.end.p0(ptr nonnull %1) #5
  ret i32 %17
}

; Function Attrs: mustprogress uwtable
define dso_local noundef i32 @"?with_alloca@@YAHH@Z"(i32 noundef %0) local_unnamed_addr #0 personality ptr @__CxxFrameHandler3 {
  %2 = alloca %struct.D, align 4
  %3 = alloca ptr, align 8
  %4 = sext i32 %0 to i64
  %5 = alloca i8, i64 %4, align 8
  call void @"?use@@YAXPEAX@Z"(ptr noundef nonnull %5)
  call void @llvm.lifetime.start.p0(ptr nonnull %2) #5
  %6 = invoke noundef ptr @"??0D@@QEAA@H@Z"(ptr noundef nonnull align 4 dereferenceable(4) %2, i32 noundef %0)
          to label %7 unwind label %12

7:                                                ; preds = %1
  %8 = load i32, ptr %2, align 4
  invoke void @"?may_throw@@YAXH@Z"(i32 noundef %8)
          to label %9 unwind label %10

9:                                                ; preds = %7
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5
  call void @llvm.lifetime.end.p0(ptr nonnull %2) #5
  br label %18

10:                                               ; preds = %7
  %11 = cleanuppad within none []
  call void @"??1D@@QEAA@XZ"(ptr noundef nonnull align 4 dead_on_return(4) dereferenceable(4) %2) #5 [ "funclet"(token %11) ]
  cleanupret from %11 unwind label %12

12:                                               ; preds = %10, %1
  %13 = catchswitch within none [label %14] unwind to caller

14:                                               ; preds = %12
  %15 = catchpad within %13 [ptr @"??_R0?AUE@@@8", i32 8, ptr %3]
  call void @"?use@@YAXPEAX@Z"(ptr noundef nonnull %5) [ "funclet"(token %15) ]
  %16 = load ptr, ptr %3, align 8, !nonnull !3, !align !4
  %17 = load i32, ptr %16, align 4
  catchret from %15 to label %18

18:                                               ; preds = %14, %9
  %19 = phi i32 [ 0, %9 ], [ %17, %14 ]
  ret i32 %19
}

; Function Attrs: mustprogress noreturn uwtable
define dso_local void @"?thrower@@YAXH@Z"(i32 noundef %0) local_unnamed_addr #4 {
  %2 = alloca %struct.E, align 4
  %3 = alloca i32, align 4
  %4 = icmp eq i32 %0, 0
  br i1 %4, label %7, label %5

5:                                                ; preds = %1
  %6 = call noundef ptr @"??0E@@QEAA@AEBU0@@Z"(ptr noundef nonnull align 4 dereferenceable(28) %2, ptr noundef nonnull align 4 dereferenceable(28) null)
  call void @_CxxThrowException(ptr nonnull %2, ptr nonnull @"_TI1?AUE@@") #6
  unreachable

7:                                                ; preds = %1
  store i32 0, ptr %3, align 4
  call void @_CxxThrowException(ptr nonnull %3, ptr nonnull @_TI1H) #6
  unreachable
}

declare dso_local noundef ptr @"??0E@@QEAA@AEBU0@@Z"(ptr noundef nonnull returned align 4 dereferenceable(28), ptr noundef nonnull align 4 dereferenceable(28)) unnamed_addr #1

attributes #0 = { mustprogress uwtable "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="8" }
attributes #1 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #2 = { nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #3 = { nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #4 = { mustprogress noreturn uwtable "no-trapping-math"="true" "probe-stack"="inline-asm" "stack-protector-buffer-size"="8" }
attributes #5 = { nounwind }
attributes #6 = { noreturn }

!llvm.module.flags = !{!1, !2}

!1 = !{i32 4, !"probe-stack", !"inline-asm"}
!2 = !{i32 7, !"uwtable", i32 2}
!3 = !{}
!4 = !{i64 4}
