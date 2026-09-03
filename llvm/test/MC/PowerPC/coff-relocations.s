# RUN: llvm-mc -triple powerpcle-pc-windows -filetype=obj %s | \
# RUN:   llvm-readobj --relocations --hex-dump=.text - | FileCheck %s

.text
.globl func
func:
  bl target
  bl LoadIconA
  bl "..?quoted@@YAXXZ"
  lis 3, target@ha
  ori 3, 3, target@l
  lis 4, .Llocal@ha
  addi 4, 4, .Llocal@l

.data
ptr:
  .long target
  .long 0
.Llocal:
  .long 0

# CHECK:      Format: COFF-PowerPC
# CHECK:      Arch: powerpcle
# CHECK:      Relocations [
# CHECK-NEXT:   Section (1) .text {
# CHECK-NEXT:     0x0 IMAGE_REL_PPC_REL24 target
# CHECK-NEXT:     0x4 IMAGE_REL_PPC_REL24 LoadIconA
# CHECK-NEXT:     0x8 IMAGE_REL_PPC_REL24 ..?quoted@@YAXXZ
# CHECK-NEXT:     0xC IMAGE_REL_PPC_REFHI target
# CHECK-NEXT:     0xC IMAGE_REL_PPC_PAIR .text
# CHECK-NEXT:     0x10 IMAGE_REL_PPC_REFLO target
# CHECK-NEXT:     0x14 IMAGE_REL_PPC_REFHI .data
# CHECK-NEXT:     0x14 IMAGE_REL_PPC_PAIR
# CHECK-NEXT:     0x18 IMAGE_REL_PPC_REFLO .data
# CHECK-NEXT:   }
# CHECK-NEXT:   Section (2) .data {
# CHECK-NEXT:     0x0 IMAGE_REL_PPC_ADDR32 target
# CHECK-NEXT:   }
# CHECK-NEXT: ]
# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 01000048 fdffff4b f9ffff4b 0000603c
# CHECK-NEXT: 0x00000010 00006360 0000803c 08008438
