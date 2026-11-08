## Invalid NTUW rules are diagnosed.
# RUN: not llvm-mc -triple mipsel-pc-windows-msvc19.33 -filetype=obj %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s
# RUN: not llvm-mc -triple mipsel-pc-windows-msvc12.0 %s -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=LEGACY

	.text
f:
	.seh_proc f
# CHECK: [[@LINE+1]]:2: error: invalid NTUW rule: SAVE_GPR names the zero register or sp
	.seh_save_gpr $29, -4
# CHECK: [[@LINE+1]]:2: error: invalid NTUW rule: SAVE_FPR names an invalid floating-point register
	.seh_save_fpr $f21, -8
# CHECK: [[@LINE+1]]:2: error: invalid NTUW rule: SAVE_SPR names no special register of this machine
	.seh_save_spr lr, 8
# CHECK: [[@LINE+1]]:2: error: invalid NTUW rule: SET_CFA offset is not four-byte aligned
	.seh_set_cfa $29, 6
# CHECK: [[@LINE+1]]:2: error: the NTUW establisher must be sp or a nonvolatile register, or the CFA, with a 32-bit offset
	.seh_establisher $8, 0
	.seh_endprologue
	nop
# CHECK: [[@LINE+1]]:2: error: NTUW state rule outside the prologue and the epilogues of f
	.seh_same_gpr 16
	jr	$ra
	.seh_endproc

## An NT 4.0 target has no state rules.
# LEGACY: error: unknown directive
