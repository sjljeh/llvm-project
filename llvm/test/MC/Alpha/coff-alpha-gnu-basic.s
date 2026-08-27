# REQUIRES: alpha-registered-target
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --file-headers --hex-dump=.text %t.obj | FileCheck %s

.text
.globl start
start:
  unop
  mb
  wmb
  call_pal 0x83
  addl $1,$2,$3
  addq $4,5,$6
  subl $7,$8,$9
  subq $10,11,$12
  mull $13,$14,$15
  mulq $16,17,$18
  and $19,$20,$21
  bis $22,23,$24
  xor $25,$26,$27
  lda $1,16($30)
  ldq $2,24($30)
  stq $3,32($30)
  zapnot $16,15,$0
  cmpeq $2,$0,$2
  cmoveq $2,4,$0
  stt $f21,0($2)
  ldt $f0,-40($2)
  ret

# CHECK: Format: COFF-Alpha
# CHECK: AddressSize: 32bit
# CHECK: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
# CHECK: Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 0000fe2f 00400060 00440060 83000000
# CHECK-NEXT: 0x00000010 03002240 06b48040 2901e840 2c754141
# CHECK-NEXT: 0x00000020 0f00ae4d 1234024e 15007446 18f4c246
# CHECK-NEXT: 0x00000030 1b083a47 10003e20 18005ea4 20007eb4
# CHECK-NEXT: 0x00000040 20f6014a a2054040 80944044 0000a29e
# CHECK-NEXT: 0x00000050 d8ff028c 0180fa6b
