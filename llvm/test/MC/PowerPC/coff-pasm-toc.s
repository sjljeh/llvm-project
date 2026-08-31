# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --sections --relocations --hex-dump=.text \
# RUN:   --hex-dump=.ydata %t.obj | FileCheck %s

# These section flags, relocations, register aliases, and instruction bytes
# agree with IBM PowerPC assembler 1.0.

# CHECK:      Name: .ydata
# CHECK:      RawDataSize: 4
# CHECK:      IMAGE_SCN_ALIGN_8BYTES
# CHECK:      IMAGE_SCN_CNT_INITIALIZED_DATA
# CHECK:      IMAGE_SCN_MEM_READ
# CHECK-NOT:  IMAGE_SCN_MEM_WRITE

# CHECK:      Section {{.*}} .text {
# CHECK-NEXT: 0x0 IMAGE_REL_PPC_TOCREL16 external_descriptor
# CHECK-NEXT: }
# CHECK:      Section {{.*}} .ydata {
# CHECK-NEXT: 0x0 IMAGE_REL_PPC_ADDR32 ..probe
# CHECK-NEXT: }

# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 0000c280 f8ff2194 00002260 2000804e
# CHECK:      Hex dump of section '.ydata':
# CHECK-NEXT: 0x00000000 00000000

        .extern external_descriptor

        .text
        .globl ..probe
..probe:
        lwz r.6,[toc]external_descriptor(rtoc)
        stwu sp,-8(sp)
        ori rtoc,sp,0
        blr

        .ydata
        .align 2
        .long ..probe