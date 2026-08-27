# RUN: llvm-mc -triple=alpha-pc-windows-msvc -filetype=obj %s -o - | llvm-readobj --hex-dump=.text - | FileCheck %s

.text
.globl start
start:
  mb
  bis $31,$31,$31
  lda $0,1($31)
  addl $16,$17,$0
  beq $16,2
  br 1
  bsr -7
  call_pal 0x83
  ret

# CHECK: 0x00000000 00400060 1f04ff47 01001f20 00001142
# CHECK: 0x00000010 020000e6 0100e0c3 f9ff5fd3 83000000
# CHECK: 0x00000020 0180fa6b
