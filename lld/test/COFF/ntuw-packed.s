# REQUIRES: mips
## A function whose rules are the entry state at every PC gets a packed
## frameless .pdata entry and no .xdata record. An image whose first entry is
## packed is NTUW: an NT 4.0 entry's EndAddress is never odd.

# RUN: llvm-mc -triple mipsel-pc-windows-msvc19.33 -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=OBJ
# RUN: lld-link -dll -noentry -out:%t.dll %t.obj
# RUN: llvm-readobj --unwind %t.dll | FileCheck %s
# RUN: llvm-objdump --unwind-info %t.dll | FileCheck %s --check-prefix=OBJDUMP

# OBJ:      UnwindFormat: NTUW
# OBJ:      RuntimeFunction {
# OBJ-NEXT:   BeginAddress: .text+0x0
# OBJ-NEXT:   UnwindData: 0x11
# OBJ-NEXT:   Packed: Frameless
# OBJ-NEXT:   FunctionLength: 0x8
# OBJ-NEXT: }

# CHECK:      UnwindFormat: NTUW
# CHECK:      RuntimeFunction {
# CHECK-NEXT:   BeginAddress: 0x1000
# CHECK-NEXT:   UnwindData: 0x11
# CHECK-NEXT:   Packed: Frameless
# CHECK-NEXT:   FunctionLength: 0x8
# CHECK:      RuntimeFunction {
# CHECK-NEXT:   BeginAddress: 0x1008
# CHECK-NEXT:   UnwindData: 0x{{[0-9A-F]+}}
# CHECK-NEXT:   UnwindInfo {
# CHECK-NEXT:     Magic: 0x5755544E

# OBJDUMP:      NTUW Function Table:
# OBJDUMP-NEXT:   Begin Address: 0x1000
# OBJDUMP-NEXT:   Unwind Data: 0x11
# OBJDUMP-NEXT:   Packed: Frameless, Function Length: 0x8

	.set	noreorder
	.text
	.globl	leaf
leaf:
	.seh_proc leaf
	.seh_endprologue
	jr	$ra
	addu	$2, $4, $5
	.seh_endproc

	.globl	nonleaf
nonleaf:
	.seh_proc nonleaf
	addiu	$sp, $sp, -24
	.seh_set_cfa $sp, 24
	sw	$ra, 20($sp)
	.seh_save_gpr $ra, -4
	.seh_endprologue
	jal	leaf
	nop
	.seh_startepilogue
	lw	$ra, 20($sp)
	.seh_same_gpr $ra
	jr	$ra
	addiu	$sp, $sp, 24
	.seh_set_cfa $sp, 0
	.seh_endepilogue
	.seh_endproc
