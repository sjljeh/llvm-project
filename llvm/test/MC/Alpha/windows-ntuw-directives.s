## NTUW on Alpha: 64-bit register images and the object marker.
# RUN: llvm-mc -triple alpha-pc-windows-msvc19.33 -mattr=+taso -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --unwind --symbols %t.obj | FileCheck %s
## Software register names are accepted and printed as numbers.
# RUN: llvm-mc -triple alpha-pc-windows-msvc19.33 -mattr=+taso %s \
# RUN:   | FileCheck %s --check-prefix=ASM
# ASM: .seh_save_gpr $26, -32
# ASM: .seh_set_cfa $30, 0

	.text
f:
	.seh_proc f
	lda $30,-32($30)
	.seh_set_cfa $30, 32
	stq $26,0($30)
	.seh_save_gpr $ra, -32
	stt $f2,8($30)
	.seh_save_fpr $f2, -24
	.seh_endprologue
	bsr $26,g
	.seh_startepilogue
	ldt $f2,8($30)
	.seh_same_fpr $f2
	ldq $26,0($30)
	.seh_same_gpr $26
	lda $30,32($30)
	.seh_set_cfa $sp, 0
	ret $31,($26),1
	.seh_endepilogue
	.seh_endproc

# CHECK:      UnwindFormat: NTUW
# CHECK:      FunctionLength: 0x20
# CHECK-NEXT: PrologEndOffset: 0xC
# CHECK:      RequiredState: 0x3
# CHECK:      Opcode: SAVE_GPR
# CHECK-NEXT: Register: $26 (26)
# CHECK-NEXT: Operand: -32
# CHECK:      Opcode: SAVE_FPR
# CHECK-NEXT: Register: $f2 (2)
# CHECK-NEXT: Operand: -24
# CHECK:      StartOffset: 0x10
# CHECK-NEXT: EndOffset: 0x20
# CHECK:      Name: @unwind.00
