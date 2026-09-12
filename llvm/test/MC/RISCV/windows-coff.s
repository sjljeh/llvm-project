# RUN: llvm-mc -triple riscv32-w64-windows-gnu -filetype=obj %s -o %t.rv32
# RUN: llvm-readobj --file-headers %t.rv32 | FileCheck %s --check-prefix=RV32
# RUN: llvm-mc -triple riscv64-w64-windows-gnu -filetype=obj %s -o %t.rv64
# RUN: llvm-readobj --file-headers %t.rv64 | FileCheck %s --check-prefix=RV64

# RV32: Format: COFF-RISCV32
# RV32: Machine: IMAGE_FILE_MACHINE_RISCV32 (0x5032)
# RV64: Format: COFF-RISCV64
# RV64: Machine: IMAGE_FILE_MACHINE_RISCV64 (0x5064)

.text
.globl ret
ret:
  ret
