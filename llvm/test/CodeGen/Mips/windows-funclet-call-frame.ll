; RUN: llc -mtriple=mipsel-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mipsel-pc-windows-msvc -O0 < %s | FileCheck %s

; A function with an alloca has no reserved call frame, so its own calls move
; $sp. Its funclets set $fp to the parent's establisher frame, so NT could not
; recover a funclet's $sp at a call that moved it: funclet bodies keep $sp
; fixed and pass outgoing arguments at the bottom of their copy of the frame,
; which the parent's frame size includes.

declare void @use(ptr)
declare ptr @ctor(ptr, i32)
declare void @dtor(ptr) nounwind
declare void @may_throw(i32)
declare void @many(i32, i32, i32, i32, i32, ptr)
declare i32 @__CxxFrameHandler(...)

@"??_7type_info@@6B@" = external constant ptr
@"??_R0H@8" = linkonce_odr global { ptr, ptr, [3 x i8] } { ptr @"??_7type_info@@6B@", ptr null, [3 x i8] c".H\00" }

define i32 @alloca_funclets(i32 %n) personality ptr @__CxxFrameHandler {
entry:
  %a = alloca i32, align 4
  %e = alloca ptr, align 4
  %p = alloca i8, i32 %n, align 8
  call void @use(ptr %p)
  %c = invoke ptr @ctor(ptr %a, i32 %n)
          to label %cont unwind label %dispatch

cont:
  %v = load i32, ptr %a, align 4
  invoke void @may_throw(i32 %v)
          to label %cont1 unwind label %ehcleanup

cont1:
  call void @dtor(ptr %a)
  br label %exit

ehcleanup:
  %cp = cleanuppad within none []
  call void @dtor(ptr %a) [ "funclet"(token %cp) ]
  cleanupret from %cp unwind label %dispatch

dispatch:
  %cs = catchswitch within none [label %catch] unwind to caller

catch:
  %pad = catchpad within %cs [ptr @"??_R0H@8", i32 8, ptr %e]
  %ep = load ptr, ptr %e, align 4
  %ev = load i32, ptr %ep, align 4
  call void @many(i32 1, i32 2, i32 3, i32 4, i32 %ev, ptr %p) [ "funclet"(token %pad) ]
  catchret from %pad to label %exit

exit:
  %r = phi i32 [ 0, %cont1 ], [ %ev, %catch ]
  ret i32 %r
}

; The parent still adjusts $sp around its calls.
; CHECK-LABEL: alloca_funclets:
; CHECK:       .frame $fp,[[SIZE:[0-9]+]],$ra
; CHECK:       move $fp, $sp
; CHECK:       addiu $sp, $sp, -16
; CHECK:       jal use

; CHECK-LABEL: "?dtor${{[0-9]+}}@?0?alloca_funclets@4HA":
; CHECK:       addiu $sp, $sp, -[[SIZE]]
; CHECK:       move $fp, $2
; CHECK-NEXT:  .seh_endprologue
; CHECK-NOT:   $sp, $sp
; CHECK:       jal dtor
; CHECK-NOT:   $sp, $sp
; CHECK:       jr $ra
; CHECK-NEXT:  addiu $sp, $sp, [[SIZE]]

; The fifth and sixth arguments go to 16 and 20 bytes above the funclet's $sp,
; below its callee-saved registers and static link (-O0 copies $sp to $2).
; CHECK-LABEL: "?catch${{[0-9]+}}@?0?alloca_funclets@4HA":
; CHECK:       addiu $sp, $sp, -[[SIZE]]
; CHECK:       move $fp, $2
; CHECK-NEXT:  .seh_endprologue
; CHECK-NOT:   $sp, $sp
; CHECK-DAG:   sw {{.*}}, 16(${{sp|2}})
; CHECK-DAG:   sw {{.*}}, 20(${{sp|2}})
; CHECK-NOT:   $sp, $sp
; CHECK:       jal many
; CHECK-NOT:   $sp, $sp
; CHECK:       jr $ra
; CHECK-NEXT:  addiu $sp, $sp, [[SIZE]]

; The same holds for a C SEH __finally funclet.
declare i32 @__C_specific_handler(...)
declare void @llvm.seh.try.begin()
declare void @llvm.seh.try.end()

define i32 @sehdyn(i32 %n) personality ptr @__C_specific_handler {
entry:
  %r = alloca i32, align 4
  %pp = alloca ptr, align 4
  store i32 0, ptr %r, align 4
  %p = alloca i8, i32 %n, align 8
  store ptr %p, ptr %pp, align 4
  invoke void @llvm.seh.try.begin()
          to label %cont unwind label %ehcleanup

cont:
  %p0 = load volatile ptr, ptr %pp, align 4
  invoke void @use(ptr %p0)
          to label %cont1 unwind label %ehcleanup

cont1:
  %p1 = load volatile ptr, ptr %pp, align 4
  %q = getelementptr inbounds i8, ptr %p1, i32 1
  %b = load volatile i8, ptr %q, align 1
  %v = sext i8 %b to i32
  store volatile i32 %v, ptr %r, align 4
  invoke void @llvm.seh.try.end()
          to label %cont2 unwind label %ehcleanup

cont2:
  call void @use(ptr null)
  ret i32 %v

ehcleanup:
  %cp = cleanuppad within none []
  call void @use(ptr null) [ "funclet"(token %cp) ]
  cleanupret from %cp unwind to caller
}

; CHECK-LABEL: sehdyn:
; CHECK:       .frame $fp,[[SSIZE:[0-9]+]],$ra
; CHECK:       addiu $sp, $sp, -16
; CHECK:       jal use
; CHECK-LABEL: "?dtor${{[0-9]+}}@?0?sehdyn@4HA":
; CHECK:       addiu $sp, $sp, -[[SSIZE]]
; CHECK:       move $fp, $2
; CHECK-NEXT:  .seh_endprologue
; CHECK-NOT:   $sp, $sp
; CHECK:       jal use
; CHECK-NOT:   $sp, $sp
; CHECK:       jr $ra
; CHECK-NEXT:  addiu $sp, $sp, [[SSIZE]]
