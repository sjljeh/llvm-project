; RUN: llc -mtriple=alpha-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=alpha-pc-windows-msvc -O0 < %s | FileCheck %s --check-prefix=O0

; C SEH in a realigned frame. llvm.localaddress is the incoming $30 rounded
; down to the frame alignment A, which is the aligned base plus the frame
; size, so escaped objects keep constant CFA offsets. A filter receives the
; establisher frame (the CFA) and llvm.eh.recoverfp masks it with
; $parent_frame_mask, -A here and -1 for a frame that is not realigned.
; A __finally funclet recomputes the parent's aligned base from its $15 with
; the parent's own `bic` into the base pointer $14.

define i32 @realigned_except(i32 %n) personality ptr @__C_specific_handler {
; CHECK-LABEL: realigned_except:
; CHECK:       lda $30,-[[SIZE:[0-9]+]]($30)
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  bic $15,63,$30
; CHECK-NEXT:  bic $15,63,$14
; CHECK-NEXT:  .seh_endprologue
; CHECK:       .Lrealigned_except$frame_escape_0 = -[[ESC:[0-9]+]]
; CHECK:       .Lrealigned_except$parent_frame_mask = -64
; CHECK:       .Lrealigned_except$parent_frame_offset = 0
; CHECK-LABEL: "?filt$0@0@realigned_except@@":
; CHECK:       ldah $0,.Lrealigned_except$parent_frame_mask($31)
; CHECK-NEXT:  lda $0,.Lrealigned_except$parent_frame_mask($0)
; CHECK-NEXT:  and $18,$0,$0
; CHECK-NEXT:  ldah $1,.Lrealigned_except$frame_escape_0($31)
; CHECK-NEXT:  lda $1,.Lrealigned_except$frame_escape_0($1)
; CHECK-NEXT:  addq $0,$1,$17
entry:
  %buf = alloca [16 x i32], align 64
  call void (...) @llvm.localescape(ptr %buf)
  store i32 %n, ptr %buf, align 64
  invoke void @use(ptr %buf)
          to label %done unwind label %dispatch

dispatch:
  %cs = catchswitch within none [label %except.ret] unwind to caller

except.ret:
  %cp = catchpad within %cs [ptr @"?filt$0@0@realigned_except@@"]
  catchret from %cp to label %except

except:
  %p1 = getelementptr inbounds i8, ptr %buf, i64 4
  %v1 = load i32, ptr %p1, align 4
  ret i32 %v1

done:
  %p2 = getelementptr inbounds i8, ptr %buf, i64 8
  %v2 = load i32, ptr %p2, align 8
  ret i32 %v2
}

define internal i32 @"?filt$0@0@realigned_except@@"(ptr %eptrs, ptr %faddr, ptr %frame) {
entry:
  %fp = call ptr @llvm.eh.recoverfp(ptr @realigned_except, ptr %frame)
  %buf = call ptr @llvm.localrecover(ptr @realigned_except, ptr %fp, i32 0)
  %v = load i32, ptr %buf, align 64
  %r = call i32 @filt(i32 %v, ptr %buf)
  ret i32 %r
}

define i32 @realigned_finally(i32 %n) personality ptr @__C_specific_handler {
; CHECK-LABEL: realigned_finally:
; CHECK:       lda $30,-[[FSIZE:[0-9]+]]($30)
; CHECK:       bis $30,$30,$15
; CHECK-NEXT:  bic $15,127,$30
; CHECK-NEXT:  bic $15,127,$14
; CHECK-NEXT:  .seh_endprologue
; CHECK:       .Lrealigned_finally$frame_escape_0 = -128
; The normal path: llvm.localaddress is the aligned base plus the frame size.
; CHECK:       lda $1,[[FSIZE]]($14)
; CHECK-LABEL: "?dtor$
; CHECK:       lda $30,-[[FSIZE]]($30)
; CHECK:       stq $14,
; CHECK:       lda $15,-[[FSIZE]]($1)
; CHECK-NEXT:  bic $15,127,$14
; CHECK-NEXT:  .seh_endprologue
; CHECK:       lda $1,[[FSIZE]]($14)
; CHECK:       .Lrealigned_finally$parent_frame_mask = -128
; O0-LABEL:    realigned_finally:
; O0:          bis $30,$30,$15
; O0-NEXT:     bic $15,127,$30
; O0-NEXT:     bic $15,127,$14
; O0-NEXT:     .seh_endprologue
; O0-LABEL:    "?dtor$
; O0:          lda $15,-{{[0-9]+}}($1)
; O0-NEXT:     bic $15,127,$14
; O0-NEXT:     .seh_endprologue
entry:
  %buf = alloca [8 x i32], align 128
  call void (...) @llvm.localescape(ptr %buf)
  store i32 %n, ptr %buf, align 128
  invoke void @use(ptr %buf)
          to label %cont unwind label %cleanup

cont:
  %la = call ptr @llvm.localaddress()
  %b1 = call ptr @llvm.localrecover(ptr @realigned_finally, ptr %la, i32 0)
  %v1 = load i32, ptr %b1, align 128
  call void @g(i32 %v1)
  %p = getelementptr inbounds i8, ptr %buf, i64 12
  %r = load i32, ptr %p, align 4
  ret i32 %r

cleanup:
  %pad = cleanuppad within none []
  %la2 = call ptr @llvm.localaddress()
  %b2 = call ptr @llvm.localrecover(ptr @realigned_finally, ptr %la2, i32 0)
  %v2 = load i32, ptr %b2, align 128
  call void @g(i32 %v2) [ "funclet"(token %pad) ]
  cleanupret from %pad unwind to caller
}

; A frame that is not realigned publishes -1.
define i32 @plain_except(i32 %n) personality ptr @__C_specific_handler {
; CHECK-LABEL: plain_except:
; CHECK-NOT:   bic $15
; CHECK:       .Lplain_except$parent_frame_mask = -1
entry:
  %x = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %x)
  store i32 %n, ptr %x
  invoke void @use(ptr %x)
          to label %done unwind label %dispatch

dispatch:
  %cs = catchswitch within none [label %except.ret] unwind to caller

except.ret:
  %cp = catchpad within %cs [ptr @"?filt$0@0@plain_except@@"]
  catchret from %cp to label %done

done:
  ret i32 0
}

define internal i32 @"?filt$0@0@plain_except@@"(ptr %eptrs, ptr %faddr, ptr %frame) {
; CHECK-LABEL: "?filt$0@0@plain_except@@":
; CHECK:       ldah $0,.Lplain_except$parent_frame_mask($31)
; CHECK-NEXT:  lda $0,.Lplain_except$parent_frame_mask($0)
; CHECK-NEXT:  and $18,$0,$0
entry:
  %fp = call ptr @llvm.eh.recoverfp(ptr @plain_except, ptr %frame)
  %x = call ptr @llvm.localrecover(ptr @plain_except, ptr %fp, i32 0)
  %v = load i32, ptr %x
  ret i32 %v
}

declare void @use(ptr)
declare void @g(i32)
declare i32 @filt(i32, ptr)
declare i32 @__C_specific_handler(...)
declare ptr @llvm.eh.recoverfp(ptr, ptr)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg)
declare ptr @llvm.localaddress()
declare void @llvm.localescape(...)
