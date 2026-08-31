# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --sections --relocations --hex-dump=.text \
# RUN:   --hex-dump=.rdata --hex-dump=.pdata --hex-dump=.reldata \
# RUN:   '--hex-dump=.debug$S' %t.obj | FileCheck %s

# Exercise source spellings used by Microsoft's PowerPC assembler. The section
# characteristics, default alignments, relocations, and bytes below agree with
# IBM PowerPC assembler 1.0 from the Microsoft Windows CE toolchain.

# CHECK:      Name: .text
# CHECK:      RawDataSize: 16
# CHECK:      Name: .rdata
# CHECK:      RawDataSize: 8
# CHECK:      IMAGE_SCN_ALIGN_8BYTES
# CHECK:      IMAGE_SCN_CNT_INITIALIZED_DATA
# CHECK:      IMAGE_SCN_MEM_READ
# CHECK-NOT:  IMAGE_SCN_MEM_WRITE
# CHECK:      Name: .pdata
# CHECK:      RawDataSize: 20
# CHECK:      IMAGE_SCN_ALIGN_4BYTES
# CHECK:      IMAGE_SCN_CNT_INITIALIZED_DATA
# CHECK:      IMAGE_SCN_MEM_READ
# CHECK-NOT:  IMAGE_SCN_MEM_WRITE
# CHECK:      Name: .reldata
# CHECK:      RawDataSize: 4
# CHECK:      IMAGE_SCN_ALIGN_8BYTES
# CHECK:      IMAGE_SCN_CNT_INITIALIZED_DATA
# CHECK:      IMAGE_SCN_MEM_READ
# CHECK:      IMAGE_SCN_MEM_WRITE
# CHECK:      Name: .debug$S
# CHECK:      RawDataSize: 22
# CHECK:      IMAGE_SCN_ALIGN_1BYTES
# CHECK:      IMAGE_SCN_CNT_INITIALIZED_DATA
# CHECK:      IMAGE_SCN_MEM_DISCARDABLE
# CHECK:      IMAGE_SCN_MEM_READ
# CHECK-NOT:  IMAGE_SCN_MEM_WRITE

# CHECK:      Section {{.*}} .rdata {
# CHECK-NEXT: 0x0 IMAGE_REL_PPC_ADDR32 ..function
# CHECK-NEXT: 0x4 IMAGE_REL_PPC_ADDR32 .toc
# CHECK-NEXT: }
# CHECK:      Section {{.*}} .pdata {
# CHECK-NEXT: 0x0 IMAGE_REL_PPC_ADDR32 ..function
# CHECK-NEXT: 0x4 IMAGE_REL_PPC_ADDR32 function.end
# CHECK-NEXT: 0x10 IMAGE_REL_PPC_ADDR32 ..function
# CHECK-NEXT: }
# CHECK:      Section {{.*}} .reldata {
# CHECK-NEXT: 0x0 IMAGE_REL_PPC_ADDR32 ..function
# CHECK-NEXT: }
# CHECK:      Section {{.*}} .debug$S {
# CHECK-NEXT: 0x8 IMAGE_REL_PPC_ADDR32 ..function
# CHECK-NEXT: 0xC IMAGE_REL_PPC_SECREL ..function
# CHECK-NEXT: 0x10 IMAGE_REL_PPC_SECTION ..function
# CHECK-NEXT: }

# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 00006360 000021d8 0000022d 2000804e
# CHECK:      Hex dump of section '.rdata':
# CHECK-NEXT: 0x00000000 00000000 00000000
# CHECK:      Hex dump of section '.pdata':
# CHECK-NEXT: 0x00000000 00000000 00000000 00000000 00000000
# CHECK-NEXT: 0x00000010 00000000
# CHECK:      Hex dump of section '.reldata':
# CHECK-NEXT: 0x00000000 00000000
# CHECK:      Hex dump of section '.debug$S':
# CHECK-NEXT: 0x00000000 01000000 04000000 00000000 00000000
# CHECK-NEXT: 0x00000010 00000361 6263

        .text
        .globl ..function
..function:
        ori r.3,r3,0
        stfd f.1,0(r.sp)
        cmpwi cr.2,r.toc,0
        blr
function.end:

        .rdata
        .align 2
        .globl function
function:
        .long ..function, .toc

        .pdata
        .align 2
        .long ..function
        .long function.end
        .long 0
        .long 0
        .long ..function

        .reldata
        .globl table
table:
        .long ..function

        .debug$S
        .ualong 1
        .uashort 4
        .uashort 0
        .ualong ..function
        .ualong [secoff]..function
        .uashort [secnum]..function
        .byte 3, "abc"
