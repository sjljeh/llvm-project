# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %s -o - | \
# RUN:   llvm-readobj --hex-dump=.text - | FileCheck %s --check-prefix=COFF
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=asm %s -o %t.s
# RUN: llvm-mc -triple=powerpcle-pc-windows -filetype=obj %t.s -o - | \
# RUN:   llvm-readobj --hex-dump=.text - | FileCheck %s --check-prefix=COFF
# RUN: not llvm-mc -triple=powerpcle-unknown-linux %s -o /dev/null 2>&1 | \
# RUN:   FileCheck %s --check-prefix=ERROR

# A defined absolute symbol takes precedence over an otherwise matching
# register name (Lr versus lr). Dotted registers remain integer primaries in
# larger expressions. PASM's .asciiz terminates each comma-separated string.

.struct 0
.space 56
Lr: .space 4
.set LT, 0
.set WAS_USER_MODE, 19

.text
  .asciiz "PowerPC", "NT"
  stw r.0, Lr(r.sp)
  crand 0, cr.0 + LT, WAS_USER_MODE

# COFF:      Hex dump of section '.text':
# COFF-NEXT: 0x00000000 506f7765 72504300 4e540000 38000190
# COFF-NEXT: 0x00000010 029a004c

# ERROR: error: unknown directive
