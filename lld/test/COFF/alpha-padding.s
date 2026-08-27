# REQUIRES: alpha
# RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o %t.obj
# RUN: lld-link /machine:alpha /entry:start /subsystem:native /nodefaultlib %t.obj /out:%t.exe
# RUN: llvm-readobj --hex-dump=.text %t.exe | FileCheck %s

.text
.globl start
start:
  ret

.section .text$next,"xr"
.p2align 4
.globl next
next:
  ret

# CHECK: 0x00402000 0180fa6b 0000fe2f 0000fe2f 0000fe2f
# CHECK: 0x00402010 0180fa6b
