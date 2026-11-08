; RUN: llc -mtriple=alpha-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 < %s | FileCheck %s --check-prefix=O0
; RUN: not llc -mtriple=alpha-pc-windows-msvc -O2 -o /dev/null \
; RUN:   %S/Inputs/windows-cxx-eh-overaligned-catch.ll 2>&1 \
; RUN:   | FileCheck %s --check-prefix=ERR

; C++ EH in a realigned frame. The NT runtime finds the EH context record at
; EstablisherFrame + EHContextDelta and copies a catch object to
; EstablisherFrame + dispCatchObj, so both stay at
; fixed CFA offsets and are addressed from $15, the unaligned frame base
; (CFA - frame size), while the over-aligned buffer moves with the aligned
; base in $14. A catch funclet gets the parent's $15 (Rfp) in $1 and
; recomputes the aligned base with the parent's own `bic`.

%rtti.TypeDescriptor2 = type { ptr, ptr, [3 x i8] }
@"??_7type_info@@6B@" = external constant ptr
@"??_R0H@8" = linkonce_odr global %rtti.TypeDescriptor2 { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".H\00" }

define i32 @catch_int(i32 %n) personality ptr @__CxxFrameHandler {
; CHECK-LABEL: catch_int:
; CHECK:       lda $30,-256($30)
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  bic $15,63,$30
; CHECK-NEXT:  bic $15,63,$14
; CHECK-NEXT:  .seh_endprologue
; The EH context record's Rfp = $15, at CFA + EHContextDelta + 4
; = $15 + 256 - 40 + 4.
; CHECK-NEXT:  stl $15,220($15)
; The buffer is at the aligned base.
; CHECK:       lda ${{[0-9]+}},128($14)
; CHECK:       .long -40 # EHContextDelta
; CHECK:       .long -4 # CatchObjOffset
; CHECK-LABEL: "?catch$1@?0?catch_int@4HA":
; CHECK:       lda $30,-256($30)
; CHECK:       bis $1,$1,$15
; CHECK-NEXT:  bic $15,63,$14
; CHECK-NEXT:  .seh_endprologue
; The catch object, at CFA - 4.
; CHECK:       lda ${{[0-9]+}},252($15)
; O0-LABEL:    catch_int:
; O0:          bis $30,$30,$15
; O0-NEXT:     bic $15,63,$30
; O0-NEXT:     bic $15,63,$14
; O0-NEXT:     .seh_endprologue
; O0-LABEL:    "?catch$1@?0?catch_int@4HA":
; O0:          bis $1,$1,$15
; O0-NEXT:     bic $15,63,$14
; O0-NEXT:     .seh_endprologue
entry:
  %buf = alloca [16 x i32], align 64
  %e = alloca i32, align 4
  store i32 %n, ptr %buf, align 64
  invoke void @use(ptr %buf)
          to label %done unwind label %dispatch

dispatch:
  %cs = catchswitch within none [label %catch] unwind to caller

catch:
  %cp = catchpad within %cs [ptr @"??_R0H@8", i32 0, ptr %e]
  call void @use(ptr %buf) [ "funclet"(token %cp) ]
  %ev = load i32, ptr %e, align 4
  %p1 = getelementptr inbounds i8, ptr %buf, i64 4
  %b1 = load i32, ptr %p1, align 4
  %s = add i32 %b1, %ev
  catchret from %cp to label %exit

done:
  %p2 = getelementptr inbounds i8, ptr %buf, i64 8
  %b2 = load i32, ptr %p2, align 8
  br label %exit

exit:
  %r = phi i32 [ %b2, %done ], [ %s, %catch ]
  ret i32 %r
}

; ERR: error: {{.*}}over-aligned catch object

declare void @use(ptr)
declare i32 @__CxxFrameHandler(...)
