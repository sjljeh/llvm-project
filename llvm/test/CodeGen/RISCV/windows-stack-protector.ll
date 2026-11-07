; RUN: llc -mtriple=riscv64-pc-windows-msvc -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-pc-windows-msvc -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
;
; /GS on RISC-V64 Windows uses the MSVC CRT's __security_cookie and
; __security_check_cookie, including in functions with a C SEH __try.

declare i32 @__C_specific_handler(...)
declare void @use(ptr)
declare void @g(i32)

; CHECK-LABEL: plain_buffer:
; CHECK:         %pcrel_hi(__security_cookie)
; CHECK:         call __security_check_cookie
define i8 @plain_buffer(i64 %i) sspstrong {
  %buf = alloca [64 x i8], align 1
  call void @use(ptr %buf)
  %p = getelementptr i8, ptr %buf, i64 %i
  %v = load i8, ptr %p
  ret i8 %v
}

; CHECK-LABEL: seh_buffer:
; CHECK:         %pcrel_hi(__security_cookie)
; CHECK:         call __security_check_cookie
define i32 @seh_buffer(ptr %q) sspstrong personality ptr @__C_specific_handler {
entry:
  %buf = alloca [64 x i8], align 1
  call void @use(ptr %buf)
  %v = load volatile i32, ptr %q, align 4
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

; UNWIND-NOT: ValidationError
