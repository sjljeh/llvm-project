# RUN: llvm-mc -triple=alpha-unknown-netbsd -filetype=obj %s -o %t.netbsd.o
# RUN: llvm-readobj --file-headers %t.netbsd.o | FileCheck %s --check-prefix=NETBSD
# RUN: llvm-mc -triple=alpha-unknown-openbsd -filetype=obj %s -o %t.openbsd.o
# RUN: llvm-readobj --file-headers %t.openbsd.o | FileCheck %s --check-prefix=OPENBSD

# NETBSD: Format: elf64-alpha
# NETBSD: OS/ABI: SystemV (0x0)
# NETBSD: Machine: EM_ALPHA (0x9026)
# OPENBSD: Format: elf64-alpha
# OPENBSD: OS/ABI: OpenBSD (0xC)
# OPENBSD: Machine: EM_ALPHA (0x9026)

.text
.globl function
function:
  ret
