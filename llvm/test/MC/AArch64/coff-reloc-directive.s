// RUN: llvm-mc -triple aarch64-windows -filetype=obj %s -o %t.obj
// RUN: llvm-readobj --relocations %t.obj | FileCheck %s

target:
  .long 0
  .reloc .-4, IMAGE_REL_ARM64_ADDR32NB, target
  .quad 0
  .reloc .-8, IMAGE_REL_ARM64_ADDR64, target

// CHECK:      Relocations [
// CHECK-NEXT:   Section (1) .text {
// CHECK-NEXT:     0x0 IMAGE_REL_ARM64_ADDR32NB target
// CHECK-NEXT:     0x4 IMAGE_REL_ARM64_ADDR64 target
// CHECK-NEXT:   }
// CHECK-NEXT: ]
