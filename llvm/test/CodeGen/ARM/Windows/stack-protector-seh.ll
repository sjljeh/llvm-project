; REQUIRES: x86-registered-target, aarch64-registered-target
; RUN: llc -mtriple=thumbv7-windows-msvc < %s | FileCheck %s --check-prefixes=CHECK,ARM
; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s --check-prefixes=CHECK,X64
; RUN: llc -mtriple=aarch64-windows-msvc < %s | FileCheck %s --check-prefixes=CHECK,A64
; RUN: llc -mtriple=i686-windows-msvc < %s | FileCheck %s --check-prefix=X86

; A function with a C SEH __try (funclet-based EH) gets its /GS cookie: the
; guard lives in the parent frame and is checked before the parent's returns,
; which funclets never reach. 32-bit x86, whose EH registration node has a
; fixed place in the frame, still goes without. A local reached only through
; llvm.localescape (from a filter or __finally) is not an address leak.

declare i32 @__C_specific_handler(...)
declare void @use(ptr)
declare void @g(i32)
declare void @llvm.localescape(...)
declare ptr @llvm.localrecover(ptr, ptr, i32)

; CHECK-LABEL: seh_buffer:
; ARM:         __security_cookie
; ARM:         bl __security_check_cookie
; X64:         __security_cookie
; X64:         callq __security_check_cookie
; A64:         __security_cookie
; A64:         bl __security_check_cookie
; X86-LABEL:   _seh_buffer:
; X86-NOT:     __security_cookie
; X86-LABEL:   _seh_escaped_scalar:
define i32 @seh_buffer(ptr %p) sspstrong personality ptr @__C_specific_handler {
entry:
  %buf = alloca [64 x i8], align 1
  call void @use(ptr %buf)
  %v = load volatile i32, ptr %p, align 4
  invoke void @g(i32 %v)
          to label %done unwind label %dispatch

dispatch:
  %cs = catchswitch within none [label %handler] unwind to caller

handler:
  %cp = catchpad within %cs [ptr null]
  catchret from %cp to label %done

done:
  %r = phi i32 [ 0, %entry ], [ -1, %handler ]
  ret i32 %r
}

; The escaped scalar is reached by the __finally funclet; no buffer, so no
; cookie, as with MSVC.
; CHECK-LABEL: seh_escaped_scalar:
; CHECK-NOT:   __security_cookie
; CHECK:       .seh_endproc
define void @seh_escaped_scalar(i32 %n) sspstrong personality ptr @__C_specific_handler {
entry:
  %n.addr = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %n.addr)
  store i32 %n, ptr %n.addr, align 4
  invoke void @g(i32 %n)
          to label %done unwind label %cleanup

cleanup:
  %cp = cleanuppad within none []
  %fp = call ptr @llvm.localaddress()
  call void @fin(ptr %fp) [ "funclet"(token %cp) ]
  cleanupret from %cp unwind to caller

done:
  ret void
}

define internal void @fin(ptr %fp) {
  %p = call ptr @llvm.localrecover(ptr @seh_escaped_scalar, ptr %fp, i32 0)
  %v = load i32, ptr %p, align 4
  call void @g(i32 %v)
  ret void
}

declare ptr @llvm.localaddress()
