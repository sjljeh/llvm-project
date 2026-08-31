# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s \
# RUN:   | llvm-readobj --relocations --hex-dump=.text - \
# RUN:   | FileCheck %s --check-prefix=COFF
# RUN: not llvm-mc -triple=powerpcle-unknown-linux -filetype=obj %s \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELF

# IBM PowerPC assembler 1.0 spells high-adjusted and low address modifiers as
# [hia] and [lo]. Keep those aliases specific to PowerPC Windows COFF.
        .text
        .extern external_address
        lis r.7,[hia]external_address
        addi r.7,r.7,[lo]external_address

# COFF:      0x0 IMAGE_REL_PPC_REFHI external_address
# COFF-NEXT: 0x0 IMAGE_REL_PPC_PAIR .text
# COFF-NEXT: 0x4 IMAGE_REL_PPC_REFLO external_address
# COFF:      Hex dump of section '.text':
# COFF-NEXT: 0x00000000 0000e03c 0000e738

# ELF-COUNT-2: error: unexpected token