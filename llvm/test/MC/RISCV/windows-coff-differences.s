# RUN: llvm-mc -triple riscv64-pc-windows-msvc -filetype=obj -o %t.obj %s
# RUN: llvm-readobj -r %t.obj | FileCheck %s

.text
.Linfo_foo:
  .asciz "foo"

.section .rdata,"dr"
.Ltable:
  .word .Linfo_foo - .Ltable

# As an extension, allow 64-bit label differences. They lower to REL32 because
# the RISC-V Windows COFF contract has no REL64 relocation.
  .quad .Linfo_foo - .Ltable

# CHECK: Format: COFF-RISCV64
# CHECK: Arch: riscv64
# CHECK: AddressSize: 64bit
# CHECK: Relocations [
# CHECK:   Section {{.*}} .rdata {
# CHECK:     0x0 IMAGE_REL_RISCV_REL32 .text
# CHECK:     0x4 IMAGE_REL_RISCV_REL32 .text
# CHECK:   }
# CHECK: ]
