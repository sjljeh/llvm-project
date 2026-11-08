; RUN: llc -mtriple=mipsel-pc-windows-msvc -O2 < %s | FileCheck %s

; The NT4 MIPS C++ runtime finds everything relative to an establisher
; frame. For a frame too large for one
; addiu, NT latches that frame after reversing the second allocation step, so
; it lies SaveArea = 56 bytes below the incoming $sp:
;  - dispCatchObj and dispUnwindHelp are relative to the outermost parent's
;    establisher frame, following static links for nested funclets;
;  - a funclet keeps its static link at its own establisher frame - 4, which
;    the runtime and nested funclets (`lw $fp, -4($fp)`) follow;
;  - a try block inside a catch belongs to that catch's FuncInfo, whose
;    unwind map stops at the catch's base state;
;  - CatchHigh is the index of the outermost enclosing try block of the same
;    FuncInfo, not a state.

%struct.D = type { i32 }

@"??_7type_info@@6B@" = external constant ptr
@"??_R0H@8" = linkonce_odr global { ptr, ptr, [3 x i8] } { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".H\00" }

declare void @use(ptr)
declare ptr @ctor(ptr, i32)
declare void @dtor(ptr) nounwind
declare void @may_throw(i32)
declare i32 @__CxxFrameHandler(...)

define i32 @split(i32 %n) personality ptr @__CxxFrameHandler {
entry:
  %buf = alloca [40000 x i8], align 1
  %outer = alloca %struct.D, align 4
  %e = alloca ptr, align 4
  %in_catch = alloca %struct.D, align 4
  %i = alloca i32, align 4
  call void @use(ptr %buf)
  %call = invoke ptr @ctor(ptr %outer, i32 %n)
          to label %invoke.cont unwind label %catch.dispatch

invoke.cont:
  %0 = load i32, ptr %outer, align 4
  invoke void @may_throw(i32 %0)
          to label %invoke.cont1 unwind label %ehcleanup

invoke.cont1:
  call void @dtor(ptr %outer)
  br label %try.cont11

ehcleanup:
  %1 = cleanuppad within none []
  call void @dtor(ptr %outer) [ "funclet"(token %1) ]
  cleanupret from %1 unwind label %catch.dispatch

catch.dispatch:
  %2 = catchswitch within none [label %catch] unwind to caller

catch:
  %3 = catchpad within %2 [ptr @"??_R0H@8", i32 8, ptr %e]
  %4 = load ptr, ptr %e, align 4
  %5 = load i32, ptr %4, align 4
  %call3 = call ptr @ctor(ptr %in_catch, i32 %5) [ "funclet"(token %3) ]
  %6 = load i32, ptr %in_catch, align 4
  invoke void @may_throw(i32 %6) [ "funclet"(token %3) ]
          to label %try.cont unwind label %catch.dispatch6

catch.dispatch6:
  %7 = catchswitch within %3 [label %catch7] unwind label %ehcleanup8

catch7:
  %8 = catchpad within %7 [ptr @"??_R0H@8", i32 0, ptr %i]
  %9 = load i32, ptr %i, align 4
  %arrayidx = getelementptr inbounds i8, ptr %buf, i32 3
  %10 = load i8, ptr %arrayidx, align 1
  %conv = sext i8 %10 to i32
  %add = add i32 %9, %conv
  catchret from %8 to label %try.cont

try.cont:
  %r.0 = phi i32 [ 0, %catch ], [ %add, %catch7 ]
  call void @dtor(ptr %in_catch) [ "funclet"(token %3) ]
  catchret from %3 to label %try.cont11

try.cont11:
  %r.1 = phi i32 [ 0, %invoke.cont1 ], [ %r.0, %try.cont ]
  ret i32 %r.1

ehcleanup8:
  %11 = cleanuppad within %3 []
  call void @dtor(ptr %in_catch) [ "funclet"(token %11) ]
  cleanupret from %11 unwind to caller
}

; The frame is 40064 bytes, allocated as 24 then 40040, so the establisher
; frame E is the incoming $sp - 24. The unwind-help bytes sit at
; 20($fp) = CFA - 40044 = E - 40020; they are cleared on entry and, for the
; try's states, where its catch returns.
; CHECK-LABEL: split:
; CHECK:       .frame $fp,40064,$ra
; CHECK:       addiu $sp, $sp, -24
; CHECK:       ori $1, $zero, 40040
; CHECK-NEXT:  subu $sp, $sp, $1
; CHECK-NEXT:  move $fp, $sp
; CHECK-NEXT:  .seh_endprologue
; CHECK-NEXT:  sw $zero, 20($fp)
; CHECK-NEXT:  sw $zero, 24($fp)
; CHECK:       $ehgcr_0_3:
; CHECK-NEXT:  sw $zero, 20($fp)
; CHECK-NEXT:  sb $zero, 24($fp)
; CHECK-NEXT:  sb $zero, 25($fp)

; CHECK-LABEL: $cppxdata$split:
; CHECK:       .long 1 # NumTryBlocks
; CHECK:       .long -40020 # UnwindHelp
; CHECK-NEXT:  .long -1 # TryBlockIndex
; CHECK-NEXT:  .long 1 # FrameNestLevel
; CHECK-LABEL: $tryMap$split:
; CHECK-NEXT:  .long 0 # TryLow
; CHECK-NEXT:  .long 1 # TryHigh
; CHECK-NEXT:  .long 0 # CatchHigh
; CHECK:       .long 12 # CatchObjOffset
; CHECK-NEXT:  .long 1 # FrameNestLevel
; CHECK-NEXT:  .long "?catch$5@?0?split@4HA"

; A funclet stores its static link once its whole frame is allocated, at
; 40036($sp) = CFA - 28 = E - 4, then reads the catch object at E + 12.
; CHECK-LABEL: "?catch$5@?0?split@4HA":
; CHECK:       addiu $sp, $sp, -24
; CHECK:       ori $1, $zero, 40040
; CHECK-NEXT:  subu $sp, $sp, $1
; CHECK-NEXT:  lui $1, 1
; CHECK-NEXT:  addu $1, $sp, $1
; CHECK-NEXT:  sw $2, -25500($1)
; CHECK-NEXT:  move $fp, $2
; CHECK-NEXT:  .seh_endprologue
; CHECK-NEXT:  lw $1, 12($fp)

; The catch funclet's FuncInfo holds the try inside it and shares the unwind
; help bytes; its unwind map stops at its base state 2.
; CHECK-LABEL: "$cppxdata$?catch$5@?0?split@4HA":
; CHECK:       .long "$stateUnwindMap$?catch$5@?0?split@4HA"
; CHECK-NEXT:  .long 1 # NumTryBlocks
; CHECK-NEXT:  .long "$tryMap$?catch$5@?0?split@4HA"
; CHECK:       .long -40020 # UnwindHelp
; CHECK-NEXT:  .long 0 # TryBlockIndex
; CHECK-NEXT:  .long 2 # FrameNestLevel
; CHECK-LABEL: "$stateUnwindMap$?catch$5@?0?split@4HA":
; CHECK-NEXT:  .long -1 # ToState
; CHECK-NEXT:  .long 0 # Action
; CHECK-NEXT:  .long 0 # ToState
; CHECK-NEXT:  .long "?dtor$4@?0?split@4HA"
; CHECK-NEXT:  .long -1 # ToState
; CHECK-NEXT:  .long 0 # Action
; CHECK-NEXT:  .long 2 # ToState
; CHECK-NEXT:  .long "?dtor$7@?0?split@4HA"
; CHECK-LABEL: "$tryMap$?catch$5@?0?split@4HA":
; CHECK-NEXT:  .long 4 # TryLow
; CHECK-NEXT:  .long 4 # TryHigh
; CHECK-NEXT:  .long 0 # CatchHigh
; CHECK:       .long 16 # CatchObjOffset
; CHECK-NEXT:  .long 2 # FrameNestLevel
; CHECK-NEXT:  .long "?catch$8@?0?split@4HA"

; The nested catch is called with the outer catch's establisher frame and
; follows its static link to the parent's.
; CHECK-LABEL: "?catch$8@?0?split@4HA":
; CHECK:       sw $2, -25500($1)
; CHECK-NEXT:  move $fp, $2
; CHECK-NEXT:  lw $fp, -4($fp)
; CHECK-NEXT:  .seh_endprologue
; CHECK:       lw $2, 16($fp)
; CHECK-LABEL: "$cppxdata$?catch$8@?0?split@4HA":
; CHECK:       .long 0 # NumTryBlocks
; CHECK:       .long -40020 # UnwindHelp
; CHECK-NEXT:  .long 0 # TryBlockIndex
; CHECK-NEXT:  .long 3 # FrameNestLevel
