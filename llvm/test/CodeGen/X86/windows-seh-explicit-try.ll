; RUN: llc -mtriple=x86_64-pc-windows-msvc < %s | FileCheck %s --check-prefix=X64
; RUN: sed -e 's/__C_specific_handler/_except_handler3/' %s | \
; RUN:   llc -mtriple=i686-pc-windows-msvc | FileCheck %s --check-prefix=X86

; Explicit SEH protects potentially faulting instructions without requiring
; the translation-unit-wide eh-asynch module flag used by /EHa.

declare void @llvm.seh.try.begin()
declare void @llvm.seh.try.end()
declare i32 @__C_specific_handler(...)

define internal i32 @filter() {
  ret i32 1
}

define i32 @load_in_try(ptr %p) personality ptr @__C_specific_handler {
entry:
  invoke void @llvm.seh.try.begin()
          to label %try unwind label %catch.dispatch

try:
  %value = load volatile i32, ptr %p
  invoke void @llvm.seh.try.end()
          to label %done unwind label %catch.dispatch

done:
  ret i32 %value

catch.dispatch:
  %cs = catchswitch within none [label %catch] unwind to caller

catch:
  %cp = catchpad within %cs [ptr @filter]
  catchret from %cp to label %recover

recover:
  ret i32 0
}

; X64-LABEL: load_in_try:
; X64: [[LOAD_BEGIN:\.Ltmp[0-9]+]]:
; X64: nop
; X64: movl ({{.*}}), %{{[a-z]+}}
; X64: [[LOAD_END:\.Ltmp[0-9]+]]:
; X64: .seh_handlerdata
; X64: .long [[LOAD_BEGIN]]@IMGREL
; X64-NEXT: .long [[LOAD_END]]@IMGREL
; X64-NEXT: .long filter@IMGREL

; X86-LABEL: _load_in_try:
; X86: movl $0, [[STATE:-[0-9]+]](%ebp)
; X86: movl ({{.*}}), %{{[a-z]+}}
; X86: movl $-1, [[STATE]](%ebp)
