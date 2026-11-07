; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
;
; A realigned frame cannot reach its locals at a constant offset from s0. With
; funclets it publishes its base pointer s1 as the establisher frame: the
; parent addresses its locals through s1, a funclet receives the parent's s1
; in a1 and does not realign, and llvm.localaddress/llvm.localrecover agree.

target triple = "riscv64-w64-windows-gnu"

declare i32 @__C_specific_handler(...)
declare void @use(ptr)
declare void @llvm.localescape(...)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg)
declare ptr @llvm.localaddress()

; CHECK-LABEL: realigned:
; CHECK:         .seh_establisher x9, 0
; CHECK:         addi s0, sp, 128
; CHECK-NEXT:    .seh_set_cfa x8, 0
; CHECK-NEXT:    andi sp, sp, -64
; CHECK-NEXT:    mv s1, sp
; CHECK-NEXT:    .seh_endprologue
; CHECK-NEXT:  .Lrealigned$frame_escape_0 = [[ESC:[0-9]+]]
; CHECK-NEXT:    sw a0, [[ESC]](s1)
; CHECK:         mv a0, s1
; CHECK:         call fin
; CHECK:       $ehgcr_0_{{[0-9]+}}:
; CHECK-NEXT:    lw a0, [[ESC]](s1)
; CHECK:       "?dtor${{[0-9]+}}@?0?realigned@4HA":
; CHECK-NOT:     andi sp
; CHECK:         mv s1, a1
; CHECK-NEXT:    .seh_endprologue
; CHECK-NEXT:    mv a0, s1
; CHECK:         call fin
; CHECK:         .seh_endepilogue
; CHECK-LABEL: fin:
; CHECK:         li a1, [[ESC]]
; CHECK-NEXT:    add a0, a0, a1
define i32 @realigned(i32 %n) personality ptr @__C_specific_handler {
entry:
  %n.addr = alloca i32, align 4
  %big = alloca [64 x i8], align 64
  call void (...) @llvm.localescape(ptr %n.addr)
  store i32 %n, ptr %n.addr, align 4
  invoke void @use(ptr %big)
          to label %inner unwind label %catch.dispatch

inner:
  invoke void @use(ptr %big)
          to label %normal.fin unwind label %ehcleanup

normal.fin:
  %fp0 = call ptr @llvm.localaddress()
  invoke void @fin(ptr %fp0)
          to label %done unwind label %catch.dispatch

ehcleanup:
  %cp = cleanuppad within none []
  %fp1 = call ptr @llvm.localaddress()
  invoke void @fin(ptr %fp1) [ "funclet"(token %cp) ]
          to label %cleanup.done unwind label %catch.dispatch

cleanup.done:
  cleanupret from %cp unwind label %catch.dispatch

catch.dispatch:
  %cs = catchswitch within none [label %except] unwind to caller

except:
  %pad = catchpad within %cs [ptr null]
  catchret from %pad to label %except.body

except.body:
  %v = load i32, ptr %n.addr, align 4
  br label %done

done:
  %r = phi i32 [ 0, %normal.fin ], [ %v, %except.body ]
  ret i32 %r
}

define void @fin(ptr %fp) {
  %p = call ptr @llvm.localrecover(ptr @realigned, ptr %fp, i32 0)
  store i32 7, ptr %p
  ret void
}

; UNWIND-NOT:  ValidationError
; UNWIND:      EstablisherFrameOffset: 0
; UNWIND:      EstablisherRegister: x9 (9)
; UNWIND-NOT:  ValidationError
