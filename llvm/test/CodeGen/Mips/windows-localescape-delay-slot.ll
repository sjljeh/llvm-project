; RUN: llc -mtriple=mipsel-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=mipsel-pc-windows-msvc -O2 -filetype=obj < %s -o /dev/null

; LOCAL_ESCAPE emits only the frame-escape symbols. The delay slot filler
; must not move it into the delay slot of the probed alloca's first branch,
; where the symbols would never be defined.

; CHECK-LABEL: fin_dyn_split:
; CHECK:       .seh_endprologue
; CHECK-NEXT:  .Ltmp{{[0-9]+}}:
; CHECK-NEXT:  .Lfin_dyn_split$frame_escape_0 = -50024
; CHECK:       beqz
; CHECK-NEXT:  nop

declare void @use(ptr)
declare i32 @__C_specific_handler(...)
declare void @llvm.localescape(...)
declare ptr @llvm.localaddress()
declare ptr @llvm.localrecover(ptr, ptr, i32)
declare void @llvm.seh.try.begin()
declare void @llvm.seh.try.end()

define i32 @fin_dyn_split(i32 %n) "probe-stack"="inline-asm" personality ptr @__C_specific_handler {
entry:
  %np = alloca i32, align 4
  %buf = alloca [50000 x i8], align 1
  %pp = alloca ptr, align 4
  %r = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %pp)
  store i32 %n, ptr %np, align 4
  %p = alloca i8, i32 %n, align 8
  store ptr %p, ptr %pp, align 4
  store i32 0, ptr %r, align 4
  invoke void @llvm.seh.try.begin()
          to label %try unwind label %fin

try:
  %p0 = load volatile ptr, ptr %pp, align 4
  invoke void @use(ptr %p0)
          to label %body unwind label %fin

body:
  %i = load volatile i32, ptr %np, align 4
  %q = getelementptr inbounds i8, ptr %buf, i32 %i
  %b = load volatile i8, ptr %q, align 1
  %v = sext i8 %b to i32
  store volatile i32 %v, ptr %r, align 4
  invoke void @llvm.seh.try.end()
          to label %done unwind label %fin

done:
  %fp1 = call ptr @llvm.localaddress()
  %e1 = call ptr @llvm.localrecover(ptr @fin_dyn_split, ptr %fp1, i32 0)
  %p1 = load ptr, ptr %e1, align 4
  call void @use(ptr %p1)
  ret i32 %v

fin:
  %cp = cleanuppad within none []
  %fp2 = call ptr @llvm.localaddress()
  %e2 = call ptr @llvm.localrecover(ptr @fin_dyn_split, ptr %fp2, i32 0)
  %p2 = load ptr, ptr %e2, align 4
  call void @use(ptr %p2) [ "funclet"(token %cp) ]
  cleanupret from %cp unwind to caller
}
