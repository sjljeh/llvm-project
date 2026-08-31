# REQUIRES: x86-registered-target
# RUN: llvm-mc -triple=x86_64-unknown-linux -filetype=obj %s -o %t.o
# RUN: llvm-readobj --sections --hex-dump=.text %t.o | FileCheck %s

# CHECK:      Name: .text
# CHECK:      Size: 3
# CHECK-NOT:  Name: .struct
# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 040810

        .struct 4
field:  .space 4
slot:   .long 0
        .balign 16
layout_size:

        .text
        .byte field, slot, layout_size
