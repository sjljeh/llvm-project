# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o - | llvm-readobj --relocations - | FileCheck %s

# CHECK: R_ALPHA_BRSGP target 0x8

.text
  br $31,target+8 !samegp
.globl target
