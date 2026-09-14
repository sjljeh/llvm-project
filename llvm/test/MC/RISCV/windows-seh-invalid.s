# RUN: split-file %s %t
# RUN: not llvm-mc -triple riscv64-w64-windows-gnu -mattr=+c -filetype=obj %t/cfa.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=CFA
# RUN: not llvm-mc -triple riscv64-w64-windows-gnu -mattr=+c -filetype=obj %t/save.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=SAVE
# RUN: not llvm-mc -triple riscv64-w64-windows-gnu -mattr=+c -filetype=obj %t/duplicate.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=DUPLICATE
# CFA: SET_CFA uses an invalid base register
# SAVE: SAVE_GPR offset is not eight-byte aligned
# DUPLICATE: two state transitions conflict at the same code offset

#--- cfa.s
.text
.seh_proc bad
bad:
addi sp, sp, -16
.seh_set_cfa x10, 16
.seh_endprologue
ret
.seh_endproc

#--- save.s
.text
.seh_proc bad
bad:
addi sp, sp, -16
.seh_set_cfa x2, 16
sd ra, 8(sp)
.seh_save_gpr x1, -7
.seh_endprologue
ret
.seh_endproc

#--- duplicate.s
.text
.seh_proc bad
bad:
addi sp, sp, -16
.seh_set_cfa x2, 16
.seh_set_cfa x2, 16
.seh_endprologue
ret
.seh_endproc
