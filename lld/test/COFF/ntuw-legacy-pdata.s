# REQUIRES: mips
## An NTUW image holds 8-byte .pdata
## entries. A legacy object's 20-byte NT 4.0 entries are embedded in UNW_Legacy
## .xdata records, with base relocations for their absolute addresses. The
## image carries no other marker; its exception directory identifies the
## format. A legacy-only image keeps the NT 4.0 table.

# RUN: split-file %s %t
# RUN: llvm-mc -triple mipsel-pc-windows-msvc19.33 -filetype=obj %t/new.s -o %t/new.obj
# RUN: llvm-mc -triple mipsel-pc-windows-msvc12.0 -filetype=obj %t/old.s -o %t/old.obj
# RUN: lld-link -dll -noentry -out:%t/mixed.dll %t/old.obj %t/new.obj
# RUN: llvm-readobj --file-headers --unwind --coff-basereloc %t/mixed.dll \
# RUN:   | FileCheck %s
# RUN: lld-link -dll -noentry -out:%t/old.dll %t/old.obj
# RUN: llvm-readobj --file-headers --unwind %t/old.dll \
# RUN:   | FileCheck %s --check-prefix=OLD
## An NT 4.0 loader cannot read NTUW tables.
# RUN: lld-link -dll -noentry -subsystem:windows,4.0 -out:%t/nt4.dll \
# RUN:   %t/old.obj %t/new.obj 2>&1 | FileCheck %s --check-prefix=NT4
# RUN: lld-link -dll -noentry -subsystem:windows,4.0 -out:%t/nt4old.dll \
# RUN:   %t/old.obj 2>&1 | count 0
# NT4: warning: the image targets Windows NT 4.x but holds NTUW unwind tables, which that system cannot read; build its objects for MSVC 12.0 or earlier (for example -target mipsel-pc-windows-msvc12.0)

## llvm-objdump classifies the images the same way.
# RUN: llvm-objdump --unwind-info %t/mixed.dll | FileCheck %s --check-prefix=OBJDUMP
# RUN: llvm-objdump --unwind-info %t/old.dll | FileCheck %s --check-prefix=OBJDUMP-OLD

# OBJDUMP:      NTUW Function Table:
# OBJDUMP-NEXT:   Begin Address: 0x1000
# OBJDUMP:          Flags: 0x8
# OBJDUMP:          Legacy Entry: Begin 0x10001000, End 0x1000100C, Handler 0x0, Handler Data 0x0, Prolog End 0x10001004
# OBJDUMP:      NTUW Function Table:
# OBJDUMP-NEXT:   Begin Address: 0x100C
# OBJDUMP:          Prologue Codes:
# OBJDUMP-NEXT:       +0x0004: SET_CFA $sp, 8

# OBJDUMP-OLD:      NT 4.0 Function Table:
# OBJDUMP-OLD-NEXT:   Begin Address: 0x10001000
# OBJDUMP-OLD-NEXT:   End Address: 0x1000100C
# OBJDUMP-OLD-NEXT:   Exception Handler: 0x0
# OBJDUMP-OLD-NEXT:   Handler Data: 0x0
# OBJDUMP-OLD-NEXT:   Prolog End Address: 0x10001004

# CHECK:      MajorLinkerVersion: 14
# CHECK:      UnwindFormat: NTUW
## Sorted by begin address: old (0x1000) first.
# CHECK:      RuntimeFunction {
# CHECK-NEXT:   BeginAddress: 0x1000
# CHECK-NEXT:   UnwindData: 0x[[#%X,REC:]]
# CHECK-NEXT:   UnwindInfo {
# CHECK-NEXT:     Magic: 0x5755544E
# CHECK-NEXT:     HeaderSize: 36
# CHECK-NEXT:     RecordSize: 56
# CHECK-NEXT:     Flags: 0x8
# CHECK-NEXT:     FunctionLength: 0xC
# CHECK-NEXT:     PrologEndOffset: 0x4
# CHECK:          LegacyEntry {
# CHECK-NEXT:       BeginAddress: 0x10001000
# CHECK-NEXT:       EndAddress: 0x1000100C
# CHECK-NEXT:       ExceptionHandler: 0x0
# CHECK-NEXT:       HandlerData: 0x0
# CHECK-NEXT:       PrologEndAddress: 0x10001004
# CHECK:      RuntimeFunction {
# CHECK-NEXT:   BeginAddress: 0x100C
# CHECK:          Flags: 0x0
# CHECK-NEXT:     FunctionLength: 0x10
# CHECK-NOT:  ValidationError
## Begin, End and PrologEnd of the embedded entry are relocated.
# CHECK:      BaseReloc [
# CHECK:        Address: 0x[[#%X,REC+36]]
# CHECK-NEXT: }
# CHECK:        Address: 0x[[#%X,REC+40]]
# CHECK-NEXT: }
# CHECK:        Address: 0x[[#%X,REC+52]]

# OLD:      MajorLinkerVersion: 14
# OLD:      UnwindFormat: NT 4.0
# OLD:      Format: NT 4.0
# OLD-NEXT: BeginAddress: 0x10001000

#--- old.s
	.set noreorder
	.text
	.globl old
old:
	.seh_proc old
	addiu $sp, $sp, -8
	.seh_endprologue
	jr $ra
	addiu $sp, $sp, 8
	.seh_endproc

#--- new.s
	.set noreorder
	.text
	.globl new
new:
	.seh_proc new
	addiu $sp, $sp, -8
	.seh_set_cfa 29, 8
	.seh_endprologue
	nop
	.seh_startepilogue
	jr $ra
	addiu $sp, $sp, 8
	.seh_set_cfa 29, 0
	.seh_endepilogue
	.seh_endproc
