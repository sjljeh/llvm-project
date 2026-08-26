# REQUIRES: powerpc-registered-target
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --file-headers --sections --symbols --relocations %t.obj | FileCheck %s --check-prefix=OBJ
# RUN: llvm-objdump -d --triple=powerpcle-pc-windows --no-show-raw-insn %t.obj | FileCheck %s --check-prefix=DISASM

# Exercise the GNU-syntax subset needed for an NT-style fixed-vector or
# freestanding boot image: explicit placement, large alignment, privileged
# register operations, raw data, reservations, and relocatable vector tables.

# OBJ:      Format: COFF-PowerPC
# OBJ:      Machine: IMAGE_FILE_MACHINE_POWERPC (0x1F0)
# OBJ:      Name: .text
# OBJ:      RawDataSize: 260
# OBJ:      IMAGE_SCN_ALIGN_32BYTES
# OBJ:      Name: .rdata
# OBJ:      RelocationCount: 2
# OBJ:      Section (4) .rdata {
# OBJ:      0x0 IMAGE_REL_PPC_ADDR32 _start
# OBJ-NEXT: 0x4 IMAGE_REL_PPC_ADDR32 vector_100
# OBJ:      Name: vector_100
# OBJ:      Value: 256

# DISASM-LABEL: <_start>:
# DISASM:         li 3, 0
# DISASM-NEXT:    lis 4, 4660
# DISASM-NEXT:    mtspr 274, 3
# DISASM-NEXT:    mtsrr0 4
# DISASM-NEXT:    mtsrr1 3
# DISASM-NEXT:    sync
# DISASM-NEXT:    isync
# DISASM-NEXT:    rfi
# DISASM-LABEL: <vector_100>:
# DISASM:         b 0x0 <_start>

.text
.globl _start
.p2align 5
_start:
  li 3, 0
  lis 4, 0x1234
  mtsprg 2, 3
  mtsrr0 4
  mtsrr1 3
  sync
  isync
  rfi

.org 0x100
vector_100:
  b _start

.section .rdata,"dr"
.p2align 2
vector_table:
  .long _start
  .long vector_100
  .byte 0x50, 0x50, 0x43, 0
  .space 4
