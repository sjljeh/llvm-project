# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o - | llvm-readobj --unwind - | FileCheck %s

# CHECK: DW_CFA_offset: reg32 -8
# CHECK-NEXT: DW_CFA_offset: reg63 -16

.text
.globl cfi_test
.type cfi_test,@function
cfi_test:
.cfi_startproc
.cfi_offset $f0, -8
.cfi_offset $f31, -16
  ret
.cfi_endproc
