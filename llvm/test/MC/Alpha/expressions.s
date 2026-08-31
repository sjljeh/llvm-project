# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o - | llvm-readobj --relocations - | FileCheck %s --check-prefix=ELF
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -filetype=obj %s -o - | llvm-readobj --relocations --hex-dump=.text - | FileCheck %s --check-prefix=COFF

# ASM: lda $1,foo+8($30)
# ELF: 0x0 R_ALPHA_GPRELLOW foo 0x8
# COFF: 0x0 IMAGE_REL_ALPHA_REFLO foo
# COFF: 0x00000000 08003e20 0180fa6b

.text
.globl test
test:
  lda $1,foo+8($30)
  ret
