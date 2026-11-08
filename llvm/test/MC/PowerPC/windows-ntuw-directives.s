## NTUW on PowerPC: special-register rules and the 8-byte .pdata entry.
# RUN: llvm-mc -triple powerpcle-pc-windows-msvc19.33 %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple powerpcle-pc-windows-msvc19.33 -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND

	.text
..f:
	.seh_proc ..f
	mflr 0
	stw 0, 8(1)
	.seh_save_spr lr, 8
	mfcr 12
	stw 12, -8(1)
	.seh_save_spr cr, -8
	stwu 1, -64(1)
	.seh_set_cfa r1, 64
	.seh_endprologue
	bl ..g
	.seh_startepilogue
	lwz 0, 72(1)
	lwz 12, 56(1)
	mtcrf 255, 12
	.seh_same_spr cr
	mtlr 0
	.seh_same_spr lr
	addi 1, 1, 64
	.seh_set_cfa 1, 0
	blr
	.seh_endepilogue
	.seh_endproc

# ASM:      .seh_save_spr 0, 8
# ASM:      .seh_save_spr 1, -8
# ASM:      .seh_set_cfa 1, 64
# ASM:      .seh_same_spr 1
# ASM:      .seh_same_spr 0

# UNWIND:      UnwindFormat: NTUW
# UNWIND:      FunctionLength: 0x30
# UNWIND-NEXT: PrologEndOffset: 0x14
# UNWIND:      PrologCodes [
# UNWIND-NEXT:   Code {
# UNWIND-NEXT:     CodeOffset: 0x8
# UNWIND-NEXT:     Opcode: SAVE_SPR
# UNWIND-NEXT:     Register: lr (0)
# UNWIND-NEXT:     Operand: 8
# UNWIND-NEXT:   }
# UNWIND-NEXT:   Code {
# UNWIND-NEXT:     CodeOffset: 0x10
# UNWIND-NEXT:     Opcode: SAVE_SPR
# UNWIND-NEXT:     Register: cr (1)
# UNWIND-NEXT:     Operand: -8
# UNWIND-NEXT:   }
# UNWIND-NEXT:   Code {
# UNWIND-NEXT:     CodeOffset: 0x14
# UNWIND-NEXT:     Opcode: SET_CFA
# UNWIND-NEXT:     Register: r1 (1)
# UNWIND-NEXT:     Operand: 64
# UNWIND-NEXT:   }
# UNWIND-NEXT: ]
# UNWIND:      StartOffset: 0x18
# UNWIND-NEXT: EndOffset: 0x30
