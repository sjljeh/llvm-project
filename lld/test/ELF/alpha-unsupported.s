# REQUIRES: alpha
# RUN: split-file %s %t
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %t/literal.s -o %t/literal.o
# RUN: not ld.lld -o /dev/null %t/literal.o 2>&1 | FileCheck %s --check-prefix=LITERAL
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %t/ifunc.s -o %t/ifunc.o
# RUN: not ld.lld -o /dev/null %t/ifunc.o 2>&1 | FileCheck %s --check-prefix=IFUNC

# LITERAL: error: R_ALPHA_LITERAL with a non-zero addend is unsupported
# IFUNC: error: Alpha ELF IFUNC relocations are unsupported

#--- literal.s
.text
.globl _start
_start:
  ldq $1,target+8($29) !literal
  ret
.data
target:
  .quad 0
  .quad 1

#--- ifunc.s
.text
.globl _start
_start:
  ret
.globl resolver
.type resolver,@function
resolver:
  ret
.globl indirect
.type indirect,@gnu_indirect_function
.set indirect,resolver
.data
  .quad indirect
