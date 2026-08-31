# RUN: split-file %s %t
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t/valid.s -o %t.obj
# RUN: llvm-readobj --sections %t.obj | FileCheck %s --check-prefix=COFF
# RUN: not llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t/errors.s -o NUL 2>&1 | FileCheck %s --check-prefix=ERROR
# RUN: not llvm-mc -triple=i686-pc-windows -filetype=obj %t/non-ppc.s -o NUL 2>&1 | FileCheck %s --check-prefix=NONPPC
# RUN: not llvm-mc -triple=powerpcle-unknown-linux -filetype=obj %t/non-coff.s -o NUL 2>&1 | FileCheck %s --check-prefix=ELF

# IBM's PowerPC assembler uses 'c' for code and a decimal digit for a
# base-two section alignment. A missing digit selects LINK's machine default;
# for PowerPC that is 16 bytes, represented by omitting the alignment bits.

# COFF-LABEL: Name: .text
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0x60400020)
# COFF:         IMAGE_SCN_ALIGN_8BYTES

# COFF-LABEL: Name: .data
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0xC0400040)
# COFF:         IMAGE_SCN_ALIGN_8BYTES

# COFF-LABEL: Name: CODE
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0x60400020)
# COFF:         IMAGE_SCN_ALIGN_8BYTES
# COFF:         IMAGE_SCN_CNT_CODE
# COFF:         IMAGE_SCN_MEM_EXECUTE
# COFF:         IMAGE_SCN_MEM_READ

# COFF-LABEL: Name: WRITEC
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0xE0000020)
# COFF-NOT:     IMAGE_SCN_ALIGN_
# COFF:         IMAGE_SCN_CNT_CODE
# COFF:         IMAGE_SCN_MEM_EXECUTE
# COFF:         IMAGE_SCN_MEM_READ
# COFF:         IMAGE_SCN_MEM_WRITE

# COFF-LABEL: Name: ALIGN64
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0x60700020)
# COFF:         IMAGE_SCN_ALIGN_64BYTES

# COFF-LABEL: Name: DATA4
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0xC0300040)
# COFF:         IMAGE_SCN_ALIGN_4BYTES
# COFF:         IMAGE_SCN_CNT_INITIALIZED_DATA
# COFF:         IMAGE_SCN_MEM_READ
# COFF:         IMAGE_SCN_MEM_WRITE

# COFF-LABEL: Name: .CRT$XIC
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0xC0300040)

# COFF-LABEL: Name: .pdata
# COFF:       RawDataSize: 1
# COFF:       Characteristics [ (0x40300040)
# COFF:         IMAGE_SCN_ALIGN_4BYTES
# COFF:         IMAGE_SCN_CNT_INITIALIZED_DATA
# COFF:         IMAGE_SCN_MEM_READ

# ERROR: error: expected identifier in directive
# ERROR: error: expected string in directive
# ERROR: error: multiple section alignment flags

# NONPPC: error: unknown directive
# NONPPC: .new_section FOO,"crx3"
# NONPPC: error: unknown flag

# ELF: error: unknown directive
# ELF: .new_section FOO,"crx3"

#--- valid.s
        .text
        .byte 7
        .data
        .byte 8

        .new_section CODE,"crx3"
        .section CODE,"crx3"
        .byte 1

        .new_section WRITEC,"rcxw"
        .section WRITEC,"rcxw"
        .byte 2

        .new_section ALIGN64,"rcx6"
        .section ALIGN64,"rcx6"
        .byte 3

        .new_section DATA4,"drw2"
        .section DATA4,"drw2"
        .byte 4

        .section .CRT$XIC,"drw2"
        .byte 5

        .new_section .pdata
        .pdata
        .byte 6

#--- errors.s
        .new_section
        .new_section FOO,not_a_string
        .section BAD,"drw23"

#--- non-ppc.s
        .new_section FOO,"crx3"
        .section BAR,"crx3"

#--- non-coff.s
        .new_section FOO,"crx3"
