# REQUIRES: riscv-registered-target
# RUN: llvm-mc -triple=riscv64-pc-windows-gnu -filetype=obj %s -o %t
# RUN: llvm-dwarfdump --debug-info %t > %t.out 2> %t.err
# RUN: FileCheck %s < %t.out
# RUN: test ! -s %t.err

# Both the section offset and the address include a nonzero symbol offset
# and a nonzero addend. The debug reader must resolve the COFF relocations.
# CHECK: DW_TAG_compile_unit
# CHECK: DW_AT_name ("riscv64-coff")
# CHECK: DW_AT_low_pc (0x0000000000000018)
# CHECK: DW_AT_high_pc (0x0000000000000020)

        .text
        .space 16
code:
        .space 16

        .section .debug_abbrev,"dr"
        .long 0
abbrev_base:
        .long 0
abbrev:
        .uleb128 1
        .uleb128 0x11             # DW_TAG_compile_unit
        .byte 0                  # DW_CHILDREN_no
        .uleb128 0x03            # DW_AT_name
        .uleb128 0x08            # DW_FORM_string
        .uleb128 0x11            # DW_AT_low_pc
        .uleb128 0x01            # DW_FORM_addr
        .uleb128 0x12            # DW_AT_high_pc
        .uleb128 0x01            # DW_FORM_addr
        .byte 0, 0, 0

        .section .debug_info,"dr"
        .long unit_end - unit_start
unit_start:
        .short 4
        .secrel32 abbrev_base + 4
        .byte 8
        .uleb128 1
        .asciz "riscv64-coff"
        .quad code + 8
        .quad code + 16
unit_end:
