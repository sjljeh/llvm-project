# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s | \
# RUN:   llvm-readobj --hex-dump=.data --hex-dump=.text - | \
# RUN:   FileCheck %s --check-prefix=COFF
# RUN: llvm-mc -triple=powerpcle-unknown-linux -filetype=obj %s | \
# RUN:   llvm-readobj --hex-dump=.data --hex-dump=.text - | \
# RUN:   FileCheck %s --check-prefix=ELF

# IBM PowerPC assembler 1.0 uses single angle brackets for shifts. Keep this
# syntax specific to PowerPC COFF and preserve the normal comparison operators
# on other object formats. The COFF section contents agree with that assembler.

        .set UserMSR, 0x0001f931
        .data
        .long 0x10 > 2
        .long 2 < 4
        .long 1 > 2
        .long 1 < 2
        .long 0x80000000 > 31
        .long 0x10 >> 2

        .text
        lis 0, UserMSR > 16
        ori 0, 0, UserMSR & 0xffff

# COFF:      Hex dump of section '.text':
# COFF-NEXT: 0x00000000 0100003c 31f90060
# COFF:      Hex dump of section '.data':
# COFF-NEXT: 0x00000000 04000000 20000000 00000000 04000000
# COFF-NEXT: 0x00000010 01000000 04000000

# ELF:      Hex dump of section '.text':
# ELF-NEXT: 0x00000000 ffff003c 31f90060
# ELF:      Hex dump of section '.data':
# ELF-NEXT: 0x00000000 ffffffff ffffffff 00000000 ffffffff
# ELF-NEXT: 0x00000010 ffffffff 04000000
