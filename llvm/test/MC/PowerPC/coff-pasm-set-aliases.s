# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o - | \
# RUN:   llvm-readobj --hex-dump=.text - | FileCheck %s --check-prefix=COFF
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=asm %s -o %t.s
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t.s -o - | \
# RUN:   llvm-readobj --hex-dump=.text - | FileCheck %s --check-prefix=COFF
# RUN: not llvm-mc -triple=powerpcle-unknown-linux -filetype=obj %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=ERROR

# PASM treats register spellings as integer values in expressions, then folds
# absolute .set symbols wherever an instruction requires a register, SPR, or
# immediate. This lets one alias serve in ordinary and memory operands.

.set GprAlias, r.3
.set FprAlias, f.4
.set CrAlias, cr.2
.set SprAlias, lr
.set NumberAlias, 3
.set Hid0Alias, 1008
.set ExprAlias, NumberAlias + 1

.text
  addi GprAlias, GprAlias, 1
  lfd FprAlias, 0(GprAlias)
  cmpwi CrAlias, GprAlias, 0
  mfspr GprAlias, SprAlias
  addi NumberAlias, NumberAlias, 1
  mfspr GprAlias, Hid0Alias
  addi GprAlias, GprAlias, ExprAlias

# COFF:      Hex dump of section '.text':
# COFF-NEXT: 0x00000000 01006338 000083c8 0000032d a602687c
# COFF-NEXT: 0x00000010 01006338 a6fa707c 04006338

# ERROR: error: invalid operand for instruction
