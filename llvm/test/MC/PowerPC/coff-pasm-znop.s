# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s \
# RUN:   | llvm-readobj --relocations --hex-dump=.text - \
# RUN:   | FileCheck %s --check-prefix=COFF
# RUN: not llvm-mc -triple=powerpcle-unknown-linux -filetype=obj %s \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELF

# IBM PowerPC assembler 1.0 emits a nop with an IFGLUE relocation. The linker
# replaces the nop with the callee's TOC-restoring instruction when needed.
        .text
        .extern ..target
        bl ..target
        .znop ..target
        nop

# COFF:      0x0 IMAGE_REL_PPC_REL24 ..target
# COFF-NEXT: 0x4 IMAGE_REL_PPC_IFGLUE ..target
# COFF:      Hex dump of section '.text':
# COFF-NEXT: 0x00000000 01000048 00000060 00000060

# ELF: error: unknown directive
# ELF-NEXT: .znop ..target