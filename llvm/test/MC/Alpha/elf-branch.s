# RUN: llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o %t.o
# RUN: llvm-readobj --relocations --hex-dump=.text %t.o | FileCheck %s

# A resolved branch uses (S - (P + 4)) >> 2.  From offset 0 to offset 8 the
# encoded displacement is one instruction and no relocation remains.
# CHECK: Relocations [
# CHECK-NEXT: ]
# CHECK: 0x00000000 0100e0c3 0000ff23 0180fa6b

.text
start:
  br $31,target
  lda $31,0($31)
target:
  ret
