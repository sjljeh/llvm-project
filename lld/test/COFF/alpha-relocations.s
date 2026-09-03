# REQUIRES: alpha
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=OBJ
# RUN: lld-link /machine:alpha /entry:start /subsystem:native /nodefaultlib %t.obj /out:%t.exe
# RUN: llvm-readobj --hex-dump=.text --hex-dump=.data %t.exe | FileCheck %s --check-prefix=EXE
# RUN: llvm-readobj --coff-basereloc %t.exe | FileCheck %s --check-prefix=BASEREL
# RUN: llvm-objdump -s -j .reloc %t.exe | FileCheck %s --check-prefix=BASEREL-HEX
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -filetype=obj %s -o %t.alpha64.obj
# RUN: lld-link /machine:alpha64 /entry:start /subsystem:native /nodefaultlib %t.alpha64.obj /out:%t.alpha64.exe
# RUN: llvm-readobj --hex-dump=.text --hex-dump=.data %t.alpha64.exe | FileCheck %s --check-prefix=EXE64
# RUN: llvm-readobj --coff-basereloc %t.alpha64.exe | FileCheck %s --check-prefix=BASEREL
# RUN: llvm-objdump -s -j .reloc %t.alpha64.exe | FileCheck %s --check-prefix=BASEREL-HEX

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
  .quad target

# OBJ: Section (1) .text
# OBJ-NEXT: 0x0 IMAGE_REL_ALPHA_REFHI target
# OBJ-NEXT: 0x0 IMAGE_REL_ALPHA_PAIR
# OBJ-NEXT: 0x4 IMAGE_REL_ALPHA_REFLO target
# OBJ-NEXT: 0x8 IMAGE_REL_ALPHA_BRADDR target
# OBJ: Section (2) .data
# OBJ-NEXT: 0x4 IMAGE_REL_ALPHA_REFLONG target
# OBJ-NEXT: 0x8 IMAGE_REL_ALPHA_REFQUAD target

# EXE: Hex dump of section '.text':
# EXE-NEXT: 0x00402000 40001f24 00400020 fd07e0c3
# EXE: Hex dump of section '.data':
# EXE-NEXT: 0x00404000 00000000 00404000

# EXE64: Hex dump of section '.text':
# EXE64-NEXT: 0x140002000 00401f24 00400020 fd07e0c3
# EXE64: Hex dump of section '.data':
# EXE64-NEXT: 0x140004000 00000000 00400040

# BASEREL:      BaseReloc [
# BASEREL-NEXT:   Entry {
# BASEREL-NEXT:     Type: HIGHADJ
# BASEREL-NEXT:     Address: 0x2000
# BASEREL-NEXT:   }
# BASEREL-NEXT:   Entry {
# BASEREL-NEXT:     Type: LOW
# BASEREL-NEXT:     Address: 0x2004
# BASEREL-NEXT:   }
# BASEREL-NEXT:   Entry {
# BASEREL-NEXT:     Type: ABSOLUTE
# BASEREL-NEXT:     Address: 0x2000
# BASEREL-NEXT:   }
# BASEREL-NEXT:   Entry {
# BASEREL-NEXT:     Type: HIGHLOW
# BASEREL-NEXT:     Address: 0x4004
# BASEREL-NEXT:   }
# BASEREL-NEXT:   Entry {
# BASEREL-NEXT:     Type: DIR64
# BASEREL-NEXT:     Address: 0x4008
# BASEREL-NEXT:   }
# BASEREL-NEXT: ]

# Verify the HIGHADJ relocation's target-dependent extra word.
# BASEREL-HEX:      00200000 10000000 00400040 04200000
# BASEREL-HEX-NEXT: {{.*}} 00400000 0c000000 043008a0
