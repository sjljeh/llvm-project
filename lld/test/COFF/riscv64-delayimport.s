# REQUIRES: riscv
# RUN: split-file %s %t.dir
# RUN: llvm-dlltool -m riscv64 -d %t.dir/lib.def -l %t.dir/lib.lib
# RUN: llvm-mc -triple riscv64-w64-windows-gnu -filetype=obj %t.dir/main.s -o %t.obj
# RUN: lld-link -machine:riscv64 -entry:main -subsystem:console -delayload:lib.dll -out:%t.exe %t.obj %t.dir/lib.lib
# RUN: llvm-objdump -d --no-print-imm-hex --no-show-raw-insn -M no-aliases --mattr=+d %t.exe | FileCheck %s --check-prefix=CODE
# RUN: llvm-readobj --coff-imports --coff-basereloc %t.exe | FileCheck %s --check-prefix=TABLES
# RUN: llvm-dlltool -m riscv32 -d %t.dir/lib.def -l %t.dir/lib32.lib
# RUN: llvm-mc -triple riscv32-w64-windows-gnu -filetype=obj %t.dir/main.s -o %t.32.obj
# RUN: env LLD_IN_TEST=1 not lld-link -machine:riscv32 -entry:main -subsystem:console -delayload:lib.dll -out:%t.32.exe %t.32.obj %t.dir/lib32.lib 2>&1 | FileCheck %s --check-prefix=ERR

# Two per-symbol thunks share one tail merge. Code uses PC-relative addresses,
# not absolute/Thumb instruction relocations; only the delay IAT needs DIR64.
# The tail preserves all eight integer and floating-point argument registers
# and restores the original SP.
# CODE: Disassembly of section .text:
# CODE:      140001030: auipc t0, 2
# CODE-NEXT: 140001034: addi t0, t0, -40
# CODE-NEXT: 140001038: auipc t1, 0
# CODE-NEXT: 14000103c: jalr zero, 24(t1)
# CODE-NEXT: 140001040: auipc t0, 2
# CODE-NEXT: 140001044: addi t0, t0, -48
# CODE-NEXT: 140001048: auipc t1, 0
# CODE-NEXT: 14000104c: jalr zero, 8(t1)
# CODE-NEXT: 140001050: addi sp, sp, -144
# CODE-NEXT: 140001054: sd ra, 64(sp)
# CODE-NEXT: 140001058: sd a0, 0(sp)
# CODE-NEXT: 14000105c: sd a1, 8(sp)
# CODE-NEXT: 140001060: sd a2, 16(sp)
# CODE-NEXT: 140001064: sd a3, 24(sp)
# CODE-NEXT: 140001068: sd a4, 32(sp)
# CODE-NEXT: 14000106c: sd a5, 40(sp)
# CODE-NEXT: 140001070: sd a6, 48(sp)
# CODE-NEXT: 140001074: sd a7, 56(sp)
# CODE-NEXT: 140001078: fsd fa0, 72(sp)
# CODE-NEXT: 14000107c: fsd fa1, 80(sp)
# CODE-NEXT: 140001080: fsd fa2, 88(sp)
# CODE-NEXT: 140001084: fsd fa3, 96(sp)
# CODE-NEXT: 140001088: fsd fa4, 104(sp)
# CODE-NEXT: 14000108c: fsd fa5, 112(sp)
# CODE-NEXT: 140001090: fsd fa6, 120(sp)
# CODE-NEXT: 140001094: fsd fa7, 128(sp)
# CODE-NEXT: 140001098: addi a1, t0, 0
# CODE-NEXT: 14000109c: auipc a0, 1
# CODE-NEXT: 1400010a0: addi a0, a0, -156
# CODE-NEXT: 1400010a4: auipc ra, 0
# CODE-NEXT: 1400010a8: jalr ra, -144(ra)
# CODE-NEXT: 1400010ac: addi t0, a0, 0
# CODE-NEXT: 1400010b0: ld a0, 0(sp)
# CODE-NEXT: 1400010b4: ld a1, 8(sp)
# CODE-NEXT: 1400010b8: ld a2, 16(sp)
# CODE-NEXT: 1400010bc: ld a3, 24(sp)
# CODE-NEXT: 1400010c0: ld a4, 32(sp)
# CODE-NEXT: 1400010c4: ld a5, 40(sp)
# CODE-NEXT: 1400010c8: ld a6, 48(sp)
# CODE-NEXT: 1400010cc: ld a7, 56(sp)
# CODE-NEXT: 1400010d0: fld fa0, 72(sp)
# CODE-NEXT: 1400010d4: fld fa1, 80(sp)
# CODE-NEXT: 1400010d8: fld fa2, 88(sp)
# CODE-NEXT: 1400010dc: fld fa3, 96(sp)
# CODE-NEXT: 1400010e0: fld fa4, 104(sp)
# CODE-NEXT: 1400010e4: fld fa5, 112(sp)
# CODE-NEXT: 1400010e8: fld fa6, 120(sp)
# CODE-NEXT: 1400010ec: fld fa7, 128(sp)
# CODE-NEXT: 1400010f0: ld ra, 64(sp)
# CODE-NEXT: 1400010f4: addi sp, sp, 144
# CODE-NEXT: 1400010f8: jalr zero, 0(t0)
# TABLES: DelayImport {
# TABLES: Name: lib.dll
# TABLES: Symbol: func (0)
# TABLES: Symbol: other (0)
# TABLES: BaseReloc [
# TABLES-NEXT: Entry {
# TABLES-NEXT: Type: DIR64
# TABLES-NEXT: Address: {{.*}}
# TABLES-NEXT: }
# TABLES-NEXT: Entry {
# TABLES-NEXT: Type: DIR64
# TABLES-NEXT: Address: {{.*}}
# TABLES-NEXT: }
# TABLES-NEXT: ]
# ERR: delay loading is not supported for this machine type

#--- lib.def
LIBRARY lib.dll
EXPORTS
  func
  other

#--- main.s
.text
.option norvc
.globl main
main:
  call func
  call other
  ret
.globl __delayLoadHelper2
__delayLoadHelper2:
  ret
