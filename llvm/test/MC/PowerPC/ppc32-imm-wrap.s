# RUN: llvm-mc -triple=powerpc-unknown-linux -show-encoding %s | \
# RUN:   FileCheck %s --check-prefix=BE
# RUN: llvm-mc -triple=powerpcle-pc-windows -show-encoding %s | \
# RUN:   FileCheck %s --check-prefix=LE
# RUN: not llvm-mc -triple=powerpc64-unknown-linux -show-encoding %s 2>&1 | \
# RUN:   FileCheck %s --check-prefix=PPC64

# In 32-bit mode, unsigned spellings of negative 32-bit values are accepted
# when their signed interpretations fit the instruction operand. The first two
# forms and bytes agree with IBM PowerPC assembler 1.0. Do not truncate these
# values when assembling for a 64-bit target.

        .set high_address, 0xffffd000 + 0x5dc
        stw 1, high_address(0)
        lwz 6, 0xffffd000 + 0x5b4(0)
        addi 3, 3, 0xffff8000

# BE:      stw 1, -10788(0)               # encoding: [0x90,0x20,0xd5,0xdc]
# BE-NEXT: lwz 6, -10828(0)               # encoding: [0x80,0xc0,0xd5,0xb4]
# BE-NEXT: addi 3, 3, -32768               # encoding: [0x38,0x63,0x80,0x00]

# LE:      stw 1, -10788(0)               # encoding: [0xdc,0xd5,0x20,0x90]
# LE-NEXT: lwz 6, -10828(0)               # encoding: [0xb4,0xd5,0xc0,0x80]
# LE-NEXT: addi 3, 3, -32768               # encoding: [0x00,0x80,0x63,0x38]

# PPC64-COUNT-3: error: invalid operand for instruction