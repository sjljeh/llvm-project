## llvm/utils/ntuw-check.py accepts correct NTUW rules and reports a wrong save
## offset, a stale CFA rule in an epilogue, and a tail call that leaves the
## frame allocated.
# REQUIRES: mips-registered-target
# RUN: llvm-mc -triple mipsel-pc-windows-msvc19.33 -filetype=obj %s -o %t.obj
# RUN: not %python %llvm_src_root/utils/ntuw-check.py %t.obj | FileCheck %s

# CHECK-NOT:  good
# CHECK:      bad_offset [0x24-0x40) prologend=0x8 scopes=0: 3 bad PCs est=CFA+0
# CHECK-NEXT:    +0x8 0c000000: next=('N', ('M', -8))
# CHECK:      bad_epilogue [0x40-0x58) prologend=0x8 scopes=1: 1 bad PCs
# CHECK-NEXT:    +0x10 03e00008: sp=('S', 32)
# CHECK:      bad_tail [0x58-0x68) prologend=0x4 scopes=0: 1 bad PCs est=CFA+0
# CHECK-NEXT:    +0x4 08000000: at tail call: sp=('S', -32)
# CHECK:      4 functions, 3 bad

	.set	noreorder
	.text
good:
	.seh_proc good
	addiu	$sp, $sp, -32
	.seh_set_cfa $sp, 32
	sw	$ra, 28($sp)
	.seh_save_gpr $ra, -4
	sdc1	$f20, 16($sp)
	.seh_save_fpr $f20, -16
	.seh_endprologue
	jal	g
	nop
	.seh_startepilogue
	ldc1	$f20, 16($sp)
	.seh_same_fpr $f20
	lw	$ra, 28($sp)
	.seh_same_gpr $ra
	jr	$ra
	addiu	$sp, $sp, 32
	.seh_set_cfa $sp, 0
	.seh_endepilogue
	.seh_endproc

bad_offset:
	.seh_proc bad_offset
	addiu	$sp, $sp, -32
	.seh_set_cfa $sp, 32
	sw	$ra, 28($sp)
	.seh_save_gpr $ra, -8
	.seh_endprologue
	jal	g
	nop
	lw	$ra, 28($sp)
	jr	$ra
	addiu	$sp, $sp, 32
	.seh_endproc

bad_epilogue:
	.seh_proc bad_epilogue
	addiu	$sp, $sp, -32
	.seh_set_cfa $sp, 32
	sw	$ra, 28($sp)
	.seh_save_gpr $ra, -4
	.seh_endprologue
	.seh_startepilogue
	lw	$ra, 28($sp)
	.seh_same_gpr $ra
	addiu	$sp, $sp, 32
	jr	$ra
	nop
	.seh_endepilogue
	.seh_endproc

bad_tail:
	.seh_proc bad_tail
	addiu	$sp, $sp, -32
	.seh_set_cfa $sp, 32
	.seh_endprologue
	j	h
	nop
	addiu	$sp, $sp, 32
	.seh_endproc
