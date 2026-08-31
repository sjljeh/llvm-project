# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o - | \
# RUN:   llvm-readobj --hex-dump=.text - | FileCheck %s
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=asm %s -o %t.s
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t.s -o - | \
# RUN:   llvm-readobj --hex-dump=.text - | FileCheck %s

# PASM uses the 601-era low BO bit for prediction hints. The bit is inverted
# for backward branches. LLVM's ordinary aliases use different, direction-
# independent BO encodings, so this behavior is restricted to PPC32 WinCOFF.

# CHECK:      Hex dump of section '.text':
# CHECK-NEXT: 0x00000000 00000060 40008241 3c00a241 f4ffa241
# CHECK-NEXT: 0x00000010 f0ff8241 30000042 2c002042 e4ff2042
# CHECK-NEXT: 0x00000020 e0ff0042 20000240 1c002240 d4ff2240
# CHECK-NEXT: 0x00000030 d0ff0240 2000824d 2000a24d 2004824d
# CHECK-NEXT: 0x00000040 2004a24d 00000060 20000241 1c002241
# CHECK-NEXT: 0x00000050 18000240 14002240 10004241 0c006241
# CHECK-NEXT: 0x00000060 08004240 04006240 00000060 fcffa241
# CHECK-NEXT: 0x00000070 fcff8241 fcff2042 fcff0042

.text
back:
  nop
  beq- forward
  beq+ forward
  beq- back
  beq+ back
  bdnz- forward
  bdnz+ forward
  bdnz- back
  bdnz+ back
  bdnzf- eq, forward
  bdnzf+ eq, forward
  bdnzf- eq, back
  bdnzf+ eq, back
  beqlr-
  beqlr+
  beqctr-
  beqctr+
forward:
  nop
  bdnzt- eq, tail
  bdnzt+ eq, tail
  bdnzf- eq, tail
  bdnzf+ eq, tail
  bdzt- eq, tail
  bdzt+ eq, tail
  bdzf- eq, tail
  bdzf+ eq, tail
tail:
  nop
  beq- -4
  beq+ -4
  bdnz- -4
  bdnz+ -4
