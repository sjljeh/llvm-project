# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s | \
# RUN:   llvm-readobj --symbols --hex-dump=.text --hex-dump=.data - | \
# RUN:   FileCheck %s --check-prefix=COFF
# RUN: not llvm-mc -triple=powerpcle-unknown-linux -filetype=obj %s \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=ELF

# The .function symbol type and data directive widths agree with IBM PowerPC
# assembler 1.0. Keep its .function and .half directives, and its 4-byte .word,
# specific to PowerPC COFF.

        .text
        .globl ..function
..function:
        .function ..function
        blr

        .data
        .word 0x80000000, 0x43300000
        .half 0x1234, 0xabcd

# COFF:      Name: ..function
# COFF-NEXT: Value: 0
# COFF-NEXT: Section: .text
# COFF-NEXT: BaseType: Null
# COFF-NEXT: ComplexType: Function
# COFF-NEXT: StorageClass: External
# COFF-NEXT: AuxSymbolCount: 0
# COFF:      Hex dump of section '.text':
# COFF-NEXT: 0x00000000 2000804e
# COFF:      Hex dump of section '.data':
# COFF-NEXT: 0x00000000 00000080 00003043 3412cdab

# ELF: error: unknown directive
# ELF-NEXT: .function ..function
# ELF: literal value out of range for '.word' directive
# ELF: error: unknown directive
# ELF-NEXT: .half 0x1234, 0xabcd