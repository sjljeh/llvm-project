## .seh_splitchained under NTUW: the parent record ends at the split, and the
## chained record (UNW_ChainInfo) has no prologue; its epilogue scopes start
## from the parent's body state, and its tail is the parent's .pdata entry.
# RUN: llvm-mc -triple mipsel-pc-windows-msvc19.33 -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --unwind %t.obj | FileCheck %s
# RUN: %python %llvm_src_root/utils/ntuw-check.py -v %t.obj \
# RUN:   | FileCheck %s --check-prefix=EVERYPC

	.set	noreorder
	.text
f:
	.seh_proc f
	addiu	$sp, $sp, -24
	.seh_set_cfa $sp, 24
	sw	$ra, 20($sp)
	.seh_save_gpr $ra, -4
	.seh_endprologue
	jal	g
	nop
	beqz	$2, 1f
	nop
	.seh_startepilogue
	lw	$ra, 20($sp)
	.seh_same_gpr $ra
	jr	$ra
	addiu	$sp, $sp, 24
	.seh_set_cfa $sp, 0
	.seh_endepilogue
1:
	.seh_splitchained
	jal	g
	nop
	.seh_startepilogue
	lw	$ra, 20($sp)
	.seh_same_gpr $ra
	jr	$ra
	addiu	$sp, $sp, 24
	.seh_set_cfa $sp, 0
	.seh_endepilogue
	.seh_endproc

# CHECK:      RuntimeFunction {
# CHECK-NEXT:   BeginAddress: .text+0x0
# CHECK:          Flags: 0x0
# CHECK-NEXT:     FunctionLength: 0x24
# CHECK:        RuntimeFunction {
# CHECK-NEXT:   BeginAddress: .text+0x24
# CHECK:          Flags: 0x4
# CHECK-NEXT:     FunctionLength: 0x14
# CHECK-NEXT:     PrologEndOffset: 0x0
# CHECK:          PrologCodes [
# CHECK-NEXT:     ]
# CHECK:            EpilogScope {
# CHECK-NEXT:         StartOffset: 0x8
# CHECK-NEXT:         EndOffset: 0x14
# CHECK:          Chained {
# CHECK-NEXT:       BeginAddress: .text+0x0
# CHECK-NEXT:       UnwindData: .xdata+0x0
# CHECK-NEXT:     }

# EVERYPC: f [0x0-0x38) prologend=0x8 scopes=2 chained=1: ok est=CFA+0
