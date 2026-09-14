# REQUIRES: riscv
# RUN: llvm-mc -triple riscv64-w64-windows-gnu -filetype=obj %s -o %t.obj
# RUN: lld-link -machine:riscv64 -entry:main -subsystem:console -base:0x10000000 -out:%t.exe %t.obj
# RUN: llvm-readobj --coff-basereloc %t.exe | FileCheck %s

# CHECK:      BaseReloc [
# CHECK:        Entry {
# CHECK:          Type: RISCV_HIGH20
# CHECK:          Address: 0x1000
# CHECK:        }
# CHECK:        Entry {
# CHECK:          Type: RISCV_LOW12I
# CHECK:          Address: 0x1004
# CHECK:        }
# CHECK:        Entry {
# CHECK:          Type: RISCV_HIGH20
# CHECK:          Address: 0x1008
# CHECK:        }
# CHECK:        Entry {
# CHECK:          Type: RISCV_LOW12S
# CHECK:          Address: 0x100C
# CHECK:        }
# CHECK:      ]

.text
.globl main
main:
  lui a0, %hi(var)
  addi a0, a0, %lo(var)
  lui a1, %hi(var)
  sw a0, %lo(var)(a1)
  ret

.data
.globl var
var:
  .word 0
