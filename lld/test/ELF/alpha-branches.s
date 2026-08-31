# REQUIRES: alpha
# RUN: split-file %s %t
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %t/br.s -o %t/br.o
# RUN: llvm-readobj --relocations %t/br.o | FileCheck %s --check-prefix=BR-RELOC
# RUN: ld.lld -e caller -o %t/br.exe %t/br.o
# RUN: llvm-readobj --hex-dump=.text %t/br.exe | FileCheck %s --check-prefix=BR
# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %t/brsgp.s -o %t/brsgp.o
# RUN: llvm-readobj --relocations %t/brsgp.o | FileCheck %s --check-prefix=BRSGP-RELOC
# RUN: ld.lld -e caller -o %t/brsgp.exe %t/brsgp.o
# RUN: llvm-readobj --hex-dump=.text %t/brsgp.exe | FileCheck %s --check-prefix=BRSGP

# Both ABI branch relocations carry an addend of zero. The linker applies the
# Alpha P+4 bias. BRSGP additionally skips a callee's standard two-instruction
# GP load when STO_ALPHA_STD_GPLOAD is present.
# BR-RELOC: R_ALPHA_BRADDR callee 0x0
# BRSGP-RELOC: R_ALPHA_BRSGP callee 0x0

# BR: Hex dump of section '.text':
# BR-NEXT: 0x{{[0-9A-Fa-f]+}} 0100e0c3
# BRSGP: Hex dump of section '.text':
# BRSGP-NEXT: 0x{{[0-9A-Fa-f]+}} 0300e0c3

#--- br.s
.text
.globl caller
.type caller,@function
caller:
  br $31,callee
  lda $31,0($31)

.globl callee
.type callee,@function
callee:
  ldah $29,0($27) !gpdisp!1
  lda $29,0($29) !gpdisp!1
  ret
.usepv callee,std

#--- brsgp.s
.text
.globl caller
.type caller,@function
caller:
  br $31,callee !samegp
  lda $31,0($31)

.globl callee
.type callee,@function
callee:
  ldah $29,0($27) !gpdisp!1
  lda $29,0($29) !gpdisp!1
  ret
.usepv callee,std
