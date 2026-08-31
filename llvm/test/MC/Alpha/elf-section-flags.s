# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o - | llvm-readobj --sections - | FileCheck %s --check-prefix=OBJ
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu -defsym=INVALID=1 %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR

# OBJ: Name: .sdata
# OBJ: Flags [ (0x10000003)
# ASM: .section .sdata,"aws",@progbits
# ASM-NOT: .section .sdata,"awg",@progbits
# ERR: error: unknown flag

.ifdef INVALID
.section .sdata,"awg",@progbits
.else
.section .sdata,"aws",@progbits
.long 0
.endif
