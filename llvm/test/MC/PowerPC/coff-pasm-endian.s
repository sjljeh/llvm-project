# REQUIRES: powerpc-registered-target
# RUN: split-file %s %t
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t/valid.s -o %t.obj
# RUN: llvm-readobj --sections --relocations --hex-dump=.text \
# RUN:   --hex-dump=NOALIGN %t.obj | \
# RUN:   FileCheck %s --check-prefix=COFF
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=asm %t/valid.s -o %t.s
# RUN: FileCheck %s --check-prefix=ASM < %t.s
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t.s -o %t.round.obj
# RUN: llvm-readobj --sections --relocations --hex-dump=.text \
# RUN:   %t.round.obj | FileCheck %s --check-prefix=ROUND
# RUN: not llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t/errors.s \
# RUN:   -o NUL 2>&1 | FileCheck %s --check-prefix=ERROR
# RUN: not llvm-mc -triple=powerpcle-unknown-linux -filetype=obj \
# RUN:   %t/non-target.s -o NUL 2>&1 | FileCheck %s --check-prefix=NON-TARGET
# RUN: not llvm-mc -triple=powerpc64le-pc-windows -filetype=asm \
# RUN:   %t/non-target.s -o NUL 2>&1 | FileCheck %s --check-prefix=NON-TARGET
# RUN: not llvm-mc -triple=i686-pc-windows -filetype=obj %t/non-target.s \
# RUN:   -o NUL 2>&1 | FileCheck %s --check-prefix=NON-TARGET

# PASM switches instruction encoding, integer data, and relocatable data
# addends without changing the little-endian COFF container. It automatically
# zero-aligns instructions to four bytes. Its .align uses a base-two exponent
# and zero fill, but does not increase the external COFF section alignment.

# COFF-LABEL: Name: .text
# COFF:       RawDataSize: 65
# COFF:       RelocationCount: 1
# COFF:       Characteristics [ (0x60400020)
# COFF-NEXT:    IMAGE_SCN_ALIGN_8BYTES
# COFF-LABEL: Name: NOALIGN
# COFF:       RawDataSize: 8
# COFF:       Characteristics [ (0xE0000020)
# COFF-NOT:     IMAGE_SCN_ALIGN_
# COFF:       Section (1) .text {
# COFF-NEXT:    0x16 IMAGE_REL_PPC_ADDR32 external
# COFF:       Hex dump of section '.text':
# COFF-NEXT:  0x00000000 34126438 44332211 66550000 38641234
# COFF-NEXT:  0x00000010 11223344 55661122 3344aa00 60000000
# COFF-NEXT:  0x00000020 34126438 44332211 6655bb00 00000000
# COFF-NEXT:  0x00000030 00000000 00000000 00000000 00000000
# COFF-NEXT:  0x00000040 cc
# COFF:       Hex dump of section 'NOALIGN':
# COFF-NEXT:  0x00000000 01000000 00000060

# ROUND-LABEL: Name: .text
# ROUND:       RawDataSize: 65
# ROUND:       RelocationCount: 1
# ROUND:       Characteristics [ (0x60400020)
# ROUND-NEXT:    IMAGE_SCN_ALIGN_8BYTES
# ROUND:       Section (1) .text {
# ROUND-NEXT:    0x16 IMAGE_REL_PPC_ADDR32 external
# ROUND:       Hex dump of section '.text':
# ROUND-NEXT:  0x00000000 34126438 44332211 66550000 38641234
# ROUND-NEXT:  0x00000010 11223344 55661122 3344aa00 60000000
# ROUND-NEXT:  0x00000020 34126438 44332211 6655bb00 00000000
# ROUND-NEXT:  0x00000030 00000000 00000000 00000000 00000000
# ROUND-NEXT:  0x00000040 cc

# ERROR: error: unexpected token in '.big_endian' directive
# ERROR: error: unexpected token in '.little_endian' directive
# ERROR: error: expected alignment exponent between 0 and 31
# ERROR: error: expected alignment exponent between 0 and 31
# ERROR: error: expected absolute expression
# ERROR: error: expected newline

# ASM-NOT: <<pasm-be>>
# ASM:     .big_endian
# ASM:     .long external+{{[0-9]+}}
# ASM:     .little_endian
# ASM:     .align 5
# ASM-NOT: <<pasm-be>>

# NON-TARGET: error: unknown directive
# NON-TARGET-NEXT: .big_endian
# NON-TARGET: error: unknown directive
# NON-TARGET-NEXT: .little_endian

#--- valid.s
        .text
        .extern external
        addi r.3, r.4, 0x1234
        .ualong 0x11223344
        .uashort 0x5566

        .big_endian
        addi r.3, r.4, 0x1234
        .ualong 0x11223344
        .short 0x5566
        .long external + 0x11223344
        .byte 0xaa
        nop

        .little_endian
        addi r.3, r.4, 0x1234
        .ualong 0x11223344
        .uashort 0x5566
        .byte 0xbb
        .align 5
        .byte 0xcc

        .section NOALIGN,"rcxw"
        .byte 1
        nop

#--- errors.s
        .text
        .big_endian extra
        .little_endian 1
        .align -1
        .align 32
        .align symbol
        .align 2 extra

#--- non-target.s
        .big_endian
        .little_endian
