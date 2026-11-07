# REQUIRES: x86
# RUN: llvm-mc -triple=x86_64-windows-msvc -filetype=obj -o %t.obj %s
# RUN: lld-link -debug:symtab -entry:main %t.obj -build-id -Brepro -out:%t.exe -guard:cf
# RUN: llvm-objdump -s -t %t.exe | FileCheck %s

# Check __buildid points to 0x140002054 which is after the signature RSDS.

# CHECK:      SYMBOL TABLE:
# CHECK-NEXT: 0x00000054 __buildid
# CHECK:      Contents of section .rdata:
# CHECK-NEXT:  140002000
# CHECK-NEXT:  140002010
# CHECK-NEXT:  140002020
# CHECK-NEXT:  140002030
# CHECK-NEXT:  140002040
# CHECK-NEXT:  140002050 52534453 {{.*}}

.globl main
main:
  nop

.section .bss,"bw",discard,__buildid
.global __buildid
__buildid:

.data
.quad __buildid
