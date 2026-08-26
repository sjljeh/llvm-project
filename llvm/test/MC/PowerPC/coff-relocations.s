# RUN: llvm-mc -triple powerpcle-pc-windows -filetype=obj %s | llvm-readobj -r - | FileCheck %s

.text
.globl func
func:
  bl target
  bl LoadIconA
  lis 3, target@ha
  ori 3, 3, target@l

.data
ptr:
  .long target

# CHECK:      Format: COFF-PowerPC
# CHECK:      Arch: powerpcle
# CHECK:      Relocations [
# CHECK-NEXT:   Section (1) .text {
# CHECK-NEXT:     0x0 IMAGE_REL_PPC_REL24 target
# CHECK-NEXT:     0x4 IMAGE_REL_PPC_REL24 LoadIconA
# CHECK-NEXT:     0x8 IMAGE_REL_PPC_REFHI target
# CHECK-NEXT:     0x8 IMAGE_REL_PPC_PAIR .text
# CHECK-NEXT:     0xC IMAGE_REL_PPC_REFLO target
# CHECK-NEXT:   }
# CHECK-NEXT:   Section (2) .data {
# CHECK-NEXT:     0x0 IMAGE_REL_PPC_ADDR32 target
# CHECK-NEXT:   }
# CHECK-NEXT: ]
