# RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o - | llvm-readobj --file-headers - | FileCheck %s

.text
.globl start
start:
  .byte 0

# CHECK: Format: COFF-Alpha
# CHECK: Arch: alpha
# CHECK: Machine: IMAGE_FILE_MACHINE_ALPHA (0x184)
