# RUN: llvm-mc -triple riscv64-w64-windows-gnu -mattr=+c -filetype=obj %s -o %t.obj
# RUN: llvm-readobj -r --symbols %t.obj | FileCheck %s
# RUN: llvm-objdump -d --no-print-imm-hex --no-show-raw-insn -M no-aliases --mattr=+c %t.obj | FileCheck %s --check-prefix=DISASM

# Every RISC-V relocation type of the provisional COFF contract. The
# PC-relative low halves reference their AUIPC label, which must survive as a
# real symbol, and the split addend of a HI/LO pair is carried by the two
# instruction immediates.

.option exact
.text
.globl func
func:
.Lpcrel_hi0:
  auipc a0, %pcrel_hi(ext + 0x1234)
  addi  a0, a0, %pcrel_lo(.Lpcrel_hi0)
.Lpcrel_hi1:
  auipc a1, %pcrel_hi(local)
  sw    a0, %pcrel_lo(.Lpcrel_hi1)(a1)
  lui   a2, %hi(ext + 0x1234)
  addi  a2, a2, %lo(ext + 0x1234)
  call  ext
  jal   ext
  beq   a0, a1, ext
  c.j   ext
  c.beqz a0, ext
  nop

.data
local:
  .word ext
  .rva ext
  .quad ext
  .long ext - .
  .secrel32 ext
  .secidx ext

# CHECK:      Relocations [
# CHECK-NEXT:   Section (1) .text {
# CHECK-NEXT:     0x0 IMAGE_REL_RISCV_PCREL_HI20 ext
# CHECK-NEXT:     0x4 IMAGE_REL_RISCV_PCREL_LO12_I .Lpcrel_hi0
# CHECK-NEXT:     0x8 IMAGE_REL_RISCV_PCREL_HI20 local
# CHECK-NEXT:     0xC IMAGE_REL_RISCV_PCREL_LO12_S .Lpcrel_hi1
# CHECK-NEXT:     0x10 IMAGE_REL_RISCV_HI20 ext
# CHECK-NEXT:     0x14 IMAGE_REL_RISCV_LO12_I ext
# CHECK-NEXT:     0x18 IMAGE_REL_RISCV_CALL ext
# CHECK-NEXT:     0x20 IMAGE_REL_RISCV_JAL ext
# CHECK-NEXT:     0x24 IMAGE_REL_RISCV_BRANCH ext
# CHECK-NEXT:     0x28 IMAGE_REL_RISCV_RVC_JUMP ext
# CHECK-NEXT:     0x2A IMAGE_REL_RISCV_RVC_BRANCH ext
# CHECK-NEXT:   }
# CHECK-NEXT:   Section (2) .data {
# CHECK-NEXT:     0x0 IMAGE_REL_RISCV_ADDR32 ext
# CHECK-NEXT:     0x4 IMAGE_REL_RISCV_ADDR32NB ext
# CHECK-NEXT:     0x8 IMAGE_REL_RISCV_ADDR64 ext
# CHECK-NEXT:     0x10 IMAGE_REL_RISCV_REL32 ext
# CHECK-NEXT:     0x14 IMAGE_REL_RISCV_SECREL ext
# CHECK-NEXT:     0x18 IMAGE_REL_RISCV_SECTION ext
# CHECK-NEXT:   }
# CHECK-NEXT: ]

# The AUIPC labels are emitted as static symbols in .text.
# CHECK:      Name: .Lpcrel_hi0
# CHECK-NEXT: Value: 0
# CHECK-NEXT: Section: .text
# CHECK:      Name: .Lpcrel_hi1
# CHECK-NEXT: Value: 8
# CHECK-NEXT: Section: .text

# The addend 0x1234 is split as hi20 = 1 (rounded) and lo12 = 0x234.
# DISASM:      0: auipc a0, 1
# DISASM-NEXT: 4: addi a0, a0, 564
# DISASM:      8: auipc a1, 0
# DISASM-NEXT: c: sw a0, 0(a1)
# DISASM-NEXT: 10: lui a2, 1
# DISASM-NEXT: 14: addi a2, a2, 564
