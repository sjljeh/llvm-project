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
