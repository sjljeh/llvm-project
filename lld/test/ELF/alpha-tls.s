# REQUIRES: alpha
# RUN: split-file %s %t
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %t/ie.s -o %t/ie.o
# RUN: ld.lld -shared -o %t/ie.so %t/ie.o
# RUN: llvm-readobj --sections --relocations --dynamic-table %t/ie.so | FileCheck %s --check-prefix=IE
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %t/le.s -o %t/le.o
# RUN: ld.lld -o %t/le.exe %t/le.o
# RUN: llvm-readobj --program-headers --relocations --hex-dump=.text %t/le.exe | FileCheck %s --check-prefix=LE

# IE: Name: .rela.dyn
# IE: Type: SHT_RELA
# IE: FLAGS STATIC_TLS
# IE: R_ALPHA_TPREL64 tls 0x0

# LE: Type: PT_TLS
# LE: Relocations [
# LE-NEXT: ]
# LE: Hex dump of section '.text':
# LE-NEXT: 0x{{[0-9a-f]+}} 0300bb27 2080bd23 00000024 10000020

#--- ie.s
.text
.globl function
.type function,@function
function:
  ldah $29,0($27) !gpdisp!1
  lda $29,0($29) !gpdisp!1
  ldq $2,tls($29) !gottprel
  ret
.size function,.-function
.usepv function,std

.section .tdata,"awT",@progbits
.globl tls
.type tls,@tls_object
tls:
  .quad 1

#--- le.s
.text
.globl _start
.type _start,@function
_start:
  ldah $29,0($27) !gpdisp!1
  lda $29,0($29) !gpdisp!1
  ldah $0,tls($0) !tprelhi
  lda $0,tls($0) !tprello
  ret
.size _start,.-_start
.usepv _start,std

.section .tdata,"awT",@progbits
.local tls
.type tls,@tls_object
tls:
  .quad 2
