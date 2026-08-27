# REQUIRES: alpha-registered-target
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -filetype=obj %s -o - | llvm-readobj --file-headers - | FileCheck %s

.text
.globl start
start:
  ret

# CHECK: Format: COFF-Alpha64
# CHECK: Arch: alpha
# CHECK: AddressSize: 64bit
# CHECK: Machine: IMAGE_FILE_MACHINE_ALPHA64 (0x284)
