# REQUIRES: riscv
# RUN: llvm-mc -triple riscv64-w64-windows-gnu -filetype=obj %s -o %t.obj
# RUN: llvm-readobj -r %t.obj | FileCheck %s --check-prefix=OBJECT
# RUN: lld-link -machine:riscv64 -entry:main -subsystem:console -out:%t.exe %t.obj
# RUN: llvm-objdump -d --no-print-imm-hex --no-show-raw-insn -M no-aliases %t.exe | FileCheck %s
# RUN: llvm-mc -triple riscv32-w64-windows-gnu -filetype=obj %s -o %t.32.obj
# RUN: llvm-readobj -r %t.32.obj | FileCheck %s --check-prefix=OBJECT
# RUN: lld-link -machine:riscv32 -entry:main -subsystem:console -out:%t.32.exe %t.32.obj
# RUN: llvm-objdump -d --no-print-imm-hex --no-show-raw-insn -M no-aliases %t.32.exe | FileCheck %s

# A temporary data label at a nonzero section offset must not disappear into
# only the high instruction's implicit addend. The low instruction carries the
# expression constant, not the offset of the high target in its section. Keep
# the target symbol itself, including when a nonzero constant crosses bit 11.
# This covers address materialization and a store-form low relocation.
# OBJECT: IMAGE_REL_RISCV_PCREL_HI20 .Lvalue
# OBJECT: IMAGE_REL_RISCV_PCREL_LO12_I .Lhi0
# OBJECT: IMAGE_REL_RISCV_PCREL_HI20 .Lvalue
# OBJECT: IMAGE_REL_RISCV_PCREL_LO12_I .Lhi1
# OBJECT: IMAGE_REL_RISCV_PCREL_HI20 .Lvalue
# OBJECT: IMAGE_REL_RISCV_PCREL_LO12_I .Lhi2
# OBJECT: IMAGE_REL_RISCV_PCREL_HI20 .Lvalue
# OBJECT: IMAGE_REL_RISCV_PCREL_LO12_S .Lhi3
# CHECK:      [[BASE:[0-9a-f]+]]1000: auipc a0, 2
# CHECK-NEXT: [[BASE]]1004: addi a0, a0, 564
# CHECK-NEXT: [[BASE]]1008: auipc a1, 3
# CHECK-NEXT: [[BASE]]100c: addi a1, a1, -1488
# CHECK-NEXT: [[BASE]]1010: auipc a2, 2
# CHECK-NEXT: [[BASE]]1014: addi a2, a2, -1504
# CHECK-NEXT: [[BASE]]1018: auipc a3, 2
# CHECK-NEXT: [[BASE]]101c: sw a0, 548(a3)

.text
.option norvc
.globl main
main:
.Lhi0:
    auipc a0, %pcrel_hi(.Lvalue)
    addi a0, a0, %pcrel_lo(.Lhi0)
.Lhi1:
    auipc a1, %pcrel_hi(.Lvalue + 0x804)
    addi a1, a1, %pcrel_lo(.Lhi1)
.Lhi2:
    auipc a2, %pcrel_hi(.Lvalue - 0x804)
    addi a2, a2, %pcrel_lo(.Lhi2)
.Lhi3:
    auipc a3, %pcrel_hi(.Lvalue + 8)
    sw a0, %pcrel_lo(.Lhi3)(a3)
    ret

.data
    .zero 0x1234
.Lvalue:
    .quad 0
