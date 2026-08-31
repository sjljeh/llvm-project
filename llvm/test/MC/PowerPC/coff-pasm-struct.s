# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --sections --symbols --hex-dump=.text %t.obj | FileCheck %s

# .struct maintains an absolute layout counter; it must not emit the dummy
# storage used to calculate field offsets. The instruction bytes agree with
# IBM PowerPC assembler 1.0.

# CHECK:      Name: .text
# CHECK:      RawDataSize: 20
# CHECK-NOT:  Name: .struct
# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 04006038 08008038 1000a038 f0ff2194
# CHECK-NEXT: 0x00000010 2000804e

        .struct 4
field:  .space 4
slot:   .long 0
        .align 3
frame_size:

        .text
        li r.3,field
        li r.4,slot
        li r.5,frame_size
        stwu sp,-frame_size(sp)
        blr
