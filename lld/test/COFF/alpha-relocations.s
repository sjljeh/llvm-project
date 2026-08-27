# REQUIRES: alpha
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=OBJ
# RUN: lld-link /machine:alpha /entry:start /subsystem:native /nodefaultlib %t.obj /out:%t.exe
# RUN: llvm-readobj --hex-dump=.text --hex-dump=.data %t.exe | FileCheck %s --check-prefix=EXE

.text
.globl start
start:
  ldah $0,target($31)
  lda $0,target($0)
  br $31,target

.data
.p2align 2
target:
  .long 0
.globl ptr
ptr:
  .long target

# OBJ: Section (1) .text
# OBJ-NEXT: 0x0 IMAGE_REL_ALPHA_REFHI target
# OBJ-NEXT: 0x0 IMAGE_REL_ALPHA_PAIR
# OBJ-NEXT: 0x4 IMAGE_REL_ALPHA_REFLO target
# OBJ-NEXT: 0x8 IMAGE_REL_ALPHA_BRADDR target
# OBJ: Section (2) .data
# OBJ-NEXT: 0x4 IMAGE_REL_ALPHA_REFLONG target

# EXE: Hex dump of section '.text':
# EXE-NEXT: 0x00402000 40001f24 00400020 fd07e0c3
# EXE: Hex dump of section '.data':
# EXE-NEXT: 0x00404000 00000000 00404000
