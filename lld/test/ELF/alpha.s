# REQUIRES: alpha
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o %t.o
# RUN: ld.lld -o %t.exe %t.o
# RUN: llvm-readobj --file-headers --program-headers --sections --symbols --relocations --hex-dump=.text --hex-dump=.got %t.exe | FileCheck %s
# RUN: llvm-readelf --sections %t.exe | FileCheck %s --check-prefix=ORDER
# RUN: ld.lld -m elf64alpha_nbsd -o %t.netbsd %t.o
# RUN: llvm-readobj --file-headers %t.netbsd | FileCheck %s --check-prefix=NETBSD
# RUN: ld.lld -m elf64alpha_obsd -o %t.openbsd %t.o
# RUN: llvm-readobj --file-headers %t.openbsd | FileCheck %s --check-prefix=OPENBSD

# CHECK: Format: elf64-alpha
# CHECK: Machine: EM_ALPHA (0x9026)
# CHECK: Entry: 0x1200{{[0-9A-F]+}}
# CHECK: Name: .got
# CHECK: Size: 8
# CHECK: Type: PT_LOAD
# CHECK: Alignment: 65536
# CHECK: Relocations [
# CHECK-NEXT: ]
# CHECK: Name: _gp
# CHECK: Binding: Local
# CHECK: Other [ (0x2)
# CHECK: Section: .got
# CHECK: Name: _start
# CHECK: Other [ (0x88)
# CHECK: Hex dump of section '.text':
# CHECK-NEXT: 0x{{[0-9A-Fa-f]+}} 0200bb27 1880bd23 00803da4 0100e0c3
# CHECK-NEXT: 0x{{[0-9A-Fa-f]+}} 0000ff23 0180fa6b

# NETBSD: Machine: EM_ALPHA (0x9026)
# NETBSD: Entry: 0x1200{{[0-9A-F]+}}
# OPENBSD: Machine: EM_ALPHA (0x9026)
# OPENBSD: Entry: 0x20{{[0-9A-F]+}}

# ORDER: .got
# ORDER-NEXT: {{.*}} .sdata
# ORDER-NEXT: {{.*}} .data

.text
.globl _start
.globl __start
.type _start,@function
_start:
__start:
  ldah $29,0($27) !gpdisp!1
  lda $29,0($29) !gpdisp!1
  ldq $1,datum($29) !literal!2
  br $31,done !samegp
  lda $31,0($31)
done:
  ret
.size _start,.-_start
.usepv _start,std

.data
.globl datum
datum:
  .quad done
  .quad _gp

.section .sdata,"aws",@progbits
  .quad 1
