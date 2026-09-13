# REQUIRES: riscv

# RUN: llvm-mc -triple riscv64-w64-windows-gnu -mattr=+c -filetype=obj %s -o %t.obj
# RUN: lld-link -machine:riscv64 -entry:main -subsystem:console -out:%t.exe %t.obj
# RUN: llvm-objdump -d --no-print-imm-hex --no-show-raw-insn -M no-aliases --mattr=+c %t.exe | FileCheck %s
# RUN: llvm-readobj --coff-basereloc %t.exe | FileCheck %s --check-prefix=BASEREL
# RUN: llvm-objdump -s --section=.data %t.exe | FileCheck %s --check-prefix=DATA

# The image is placed at 0x140000000; .text lands at RVA 0x1000 and .data at
# RVA 0x2000. Both AUIPC/ADDI pairs of the same symbol are hoisted above their
# low halves, so pairing must go through the AUIPC labels rather than by
# symbol order.

.option exact
.text
.globl main
main:
.Lpcrel_hi0:
  auipc a0, %pcrel_hi(var)
.Lpcrel_hi1:
  auipc a1, %pcrel_hi(var + 0x1004)
  addi  a0, a0, %pcrel_lo(.Lpcrel_hi0)
  addi  a1, a1, %pcrel_lo(.Lpcrel_hi1)
.Lpcrel_hi2:
  auipc a2, %pcrel_hi(var)
  sw    a0, %pcrel_lo(.Lpcrel_hi2)(a2)
  call  callee
  jal   callee
  beq   a0, a1, callee
  c.j   callee
  c.beqz a0, callee
  nop

# A separate section keeps the branches unresolved at assembly time.
.section .text$callee,"xr"
.globl callee
callee:
  ret

.data
.globl var
var:
  .quad var
  .long var
  .rva var
  .long var - .

# CHECK:      140001000: auipc a0, 1
# CHECK-NEXT: 140001004: auipc a1, 2
# CHECK-NEXT: 140001008: addi a0, a0, 0
# CHECK-NEXT: 14000100c: addi a1, a1, 0
# CHECK-NEXT: 140001010: auipc a2, 1
# CHECK-NEXT: 140001014: sw a0, -16(a2)
# CHECK-NEXT: 140001018: auipc ra, 0
# CHECK-NEXT: 14000101c: jalr ra, 24(ra)
# CHECK-NEXT: 140001020: jal ra, 0x140001030
# CHECK-NEXT: 140001024: beq a0, a1, 0x140001030
# CHECK-NEXT: 140001028: c.j 0x140001030
# CHECK-NEXT: 14000102a: c.beqz a0, 0x140001030
# CHECK-NEXT: 14000102c: addi zero, zero, 0
# CHECK-NEXT: 140001030: jalr zero, 0(ra)

# DATA:      Contents of section .data:
# DATA-NEXT:  140002000 00200040 01000000 00200040 00200000
# DATA-NEXT:  140002010 f0ffffff

# BASEREL:      BaseReloc [
# BASEREL-NEXT:   Entry {
# BASEREL-NEXT:     Type: DIR64
# BASEREL-NEXT:     Address: 0x2000
# BASEREL-NEXT:   }
# BASEREL-NEXT:   Entry {
# BASEREL-NEXT:     Type: HIGHLOW
# BASEREL-NEXT:     Address: 0x2008
# BASEREL-NEXT:   }
