# REQUIRES: x86
# RUN: llvm-mc %s -filetype=obj -triple=x86_64-windows-msvc -o %t.obj
# RUN: lld-link -entry:main -nodefaultlib %t.obj -out:%t.exe
# RUN: llvm-readobj %t.exe --sections --section-data | FileCheck %s

# Section relocations against absolute symbols resolve to the last real output
# section index plus one.

.text
.global main
main:
ret

.section .rdata,"dr"
.secidx __guard_fids_table

# CHECK: Sections [
# CHECK:   Section {
# CHECK:     Number: 1
# CHECK:     Name: .text (2E 74 65 78 74 00 00 00)
# CHECK:     VirtualSize: 0x1
# CHECK:     SectionData (
# CHECK:       0000: C3                                   |.|
# CHECK:     )
# CHECK:   }
# CHECK:   Section {
# CHECK:     Number: 2
# CHECK:     Name: .rdata (2E 72 64 61 74 61 00 00)
# CHECK:     SectionData (
## The volatile metadata link.exe emits for x64 follows.
# CHECK:       0000: 03000000 18000000 00800080 00000000  |................|
# CHECK-NEXT:  0010: 00000000 00000000 00000000           |............|
# CHECK-NEXT:     )
# CHECK:   }
# CHECK-NOT: Section
# CHECK: ]
