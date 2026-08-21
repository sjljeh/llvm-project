; RUN: llc -mtriple=x86_64-pc-windows-msvc < %s | FileCheck %s --check-prefix=X64
; RUN: sed -e 's/__C_specific_handler/_except_handler3/' %s | \
; RUN:   llc -mtriple=i686-pc-windows-msvc | FileCheck %s --check-prefix=X86
; RUN: llc -mtriple=aarch64-pc-windows-msvc < %s | \
; RUN:   FileCheck %s --check-prefix=ARM64
; RUN: llc -mtriple=thumbv7-pc-windows-msvc < %s | \
; RUN:   FileCheck %s --check-prefix=ARM

declare void @may_throw()
declare void @set_bailout()
declare i32 @__C_specific_handler(...)
declare void @__IsLocalUnwind()
declare void @llvm.seh.localunwind()

define void @local_unwind() personality ptr @__C_specific_handler {
entry:
  invoke void @may_throw()
          to label %return unwind label %cleanup

return:
  ret void

cleanup:
  %cleanup.pad = cleanuppad within none []
  invoke void @set_bailout() [ "funclet"(token %cleanup.pad) ]
          to label %transfer unwind label %catch.dispatch

transfer:
  invoke void @llvm.seh.localunwind()
          to label %unreachable unwind label %catch.dispatch

unreachable:
  unreachable

catch.dispatch:
  %catch.switch = catchswitch within none [label %local.unwind] unwind to caller

local.unwind:
  %catch.pad = catchpad within %catch.switch [ptr @__IsLocalUnwind]
  catchret from %catch.pad to label %resume

resume:
  ret void
}

; X64-LABEL: local_unwind:
; X64: [[DESTINATION:\.Ltmp[0-9]+]]:{{.*}}# Block address taken
; X64: .Llsda_begin{{[0-9]+}}:
; X64-NEXT: .long
; X64-NEXT: .long
; X64-NEXT: .long "?dtor$
; X64: # Null
; X64-NEXT: .Llsda_end{{[0-9]+}}:
; X64: .seh_proc "?dtor$
; X64: leaq {{.*}}, %rcx
; X64-NEXT: leaq [[DESTINATION]](%rip), %rdx
; X64-NEXT: callq _local_unwind

; X86-LABEL: _local_unwind:
; X86: movl %esp, [[SAVED_SP:-[0-9]+]](%ebp)
; X86: .def "?dtor$
; X86: movl [[SAVED_SP]](%ebp), %esp
; X86-NEXT: jmp [[RESUME:LBB[0-9]+_[0-9]+]]
; X86: [[RESUME]]:{{.*}}# %resume
; X86: L__ehtable$local_unwind:
; X86-NEXT: .long -1
; X86-NEXT: .long 0
; X86-NEXT: .long "?dtor$

; ARM64-LABEL: local_unwind:
; ARM64: .Llsda_begin{{[0-9]+}}:
; ARM64: .word "?dtor$
; ARM64-NEXT: .word 0
; ARM64-NEXT: .Llsda_end{{[0-9]+}}:
; ARM64: .seh_proc "?dtor$
; ARM64: mov x29, x1
; ARM64: adrp x1,
; ARM64-NEXT: add x1, x1,
; ARM64-NEXT: mov x0, x29
; ARM64-NEXT: bl _local_unwind

; ARM-LABEL: local_unwind:
; ARM: .seh_proc "?dtor$
; ARM: ldr r1,
; ARM: mov r0, r11
; ARM-NEXT: bl _local_unwind
