# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --hex-dump=.text %t.obj | FileCheck %s
# RUN: llvm-objdump -d --triple=powerpcle-pc-windows --no-show-raw-insn \
# RUN:   %t.obj | FileCheck %s --check-prefix=DISASM
# RUN: not llvm-mc -triple=powerpc-unknown-linux %s 2>&1 | \
# RUN:   FileCheck %s --check-prefix=ELF

# Microsoft PPC sources use dotted condition-register fields in arithmetic
# expressions. These bytes agree with IBM PowerPC assembler 1.0.

# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 82e19c4f 42e29c4f 42e09c4f 2000804e

# DISASM:      crclr 28
# DISASM-NEXT: crset 28
# DISASM-NEXT: crnot 28, 28
# DISASM-NEXT: blr

# ELF: error: invalid operand for instruction

        .text
        crclr 4*cr.7+0
        crset 4*cr.7+0
        crnot 4*cr.7+0,4*cr.7+0
        blr