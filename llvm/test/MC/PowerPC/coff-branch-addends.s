# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --relocations --hex-dump=.text %t.obj | \
# RUN:   FileCheck %s --check-prefix=COFF
# RUN: llvm-mc -triple=powerpcle-unknown-linux -filetype=obj %s -o %t.elf
# RUN: llvm-readobj --relocations --hex-dump=.text %t.elf | \
# RUN:   FileCheck %s --check-prefix=ELF

# Microsoft PowerPC REL24 and REL14 relocations are relative to the start of
# their input section contribution. PASM therefore encodes A - O, where A is
# the source addend and O is the relocation offset. Local resolved branches
# retain the ordinary S + A - P displacement. The COFF bytes below agree with
# IBM PowerPC assembler 1.0.

        .extern target
        .text
        .long 0, 0
        bl target
        bl target+8
        beq target
        beq target+8
        nop
local:
        b local
        beq local

# COFF:      Relocations [
# COFF:        Section (1) .text {
# COFF-NEXT:     0x8 IMAGE_REL_PPC_REL24 target
# COFF-NEXT:     0xC IMAGE_REL_PPC_REL24 target
# COFF-NEXT:     0x10 IMAGE_REL_PPC_REL14 target
# COFF-NEXT:     0x14 IMAGE_REL_PPC_REL14 target
# COFF-NEXT:   }
# COFF-NEXT: ]
# COFF:      Hex dump of section '.text':
# COFF-NEXT: 0x00000000 00000000 00000000 f9ffff4b fdffff4b
# COFF-NEXT: 0x00000010 f0ff8241 f4ff8241 00000060 00000048
# COFF-NEXT: 0x00000020 fcff8241

# ELF:      0x8 R_PPC_REL24 target 0x0
# ELF-NEXT: 0xC R_PPC_REL24 target 0x8
# ELF-NEXT: 0x10 R_PPC_REL14 target 0x0
# ELF-NEXT: 0x14 R_PPC_REL14 target 0x8
# ELF:      Hex dump of section '.text':
# ELF-NEXT: 0x00000000 00000000 00000000 01000048 01000048
# ELF-NEXT: 0x00000010 00008241 00008241 00000060 00000048
# ELF-NEXT: 0x00000020 fcff8241