## NTUW state-rule directives on MIPS:
## asm round trip, the encoded record, and the @unwind.00 object marker.
# RUN: llvm-mc -triple mipsel-pc-windows-msvc19.33 %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple mipsel-pc-windows-msvc19.33 -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
# RUN: llvm-objdump --unwind-info %t.obj | FileCheck %s --check-prefix=OBJDUMP
# RUN: llvm-readobj --symbols %t.obj | FileCheck %s --check-prefix=SYM
# RUN: llvm-mc -triple mipsel-pc-windows-msvc19.33 -filetype=obj %s -o - \
# RUN:   | llvm-readobj --relocations - | FileCheck %s --check-prefix=RELOC

	.set	noreorder
	.text
	.globl	f
f:
	.seh_proc f
	addiu	$sp, $sp, -32
	.seh_set_cfa $29, 32
	sw	$ra, 28($sp)
	.seh_save_gpr $31, -4
	sdc1	$f20, 16($sp)
	.seh_save_fpr $f20, -16
	.seh_establisher cfa, -8
	.seh_endprologue
	jal	g
	nop
	.seh_startepilogue
	ldc1	$f20, 16($sp)
	.seh_same_fpr 20
	lw	$ra, 28($sp)
	.seh_same_gpr 31
	jr	$ra
	addiu	$sp, $sp, 32
	.seh_set_cfa 29, 0
	.seh_endepilogue
	.seh_endproc

# ASM:      .seh_set_cfa $sp, 32
# ASM:      .seh_save_gpr $ra, -4
# ASM:      .seh_save_fpr $f20, -16
# ASM:      .seh_establisher cfa, -8
# ASM:      .seh_same_fpr $f20
# ASM:      .seh_same_gpr $ra
# ASM:      .seh_set_cfa $sp, 0

# UNWIND:      UnwindFormat: NTUW
# UNWIND:      RuntimeFunction {
# UNWIND-NEXT:   BeginAddress: .text+0x0
# UNWIND-NEXT:   UnwindData: .xdata+0x0
# UNWIND-NEXT:   UnwindInfo {
# UNWIND-NEXT:     Magic: 0x5755544E
# UNWIND-NEXT:     HeaderSize: 36
# UNWIND-NEXT:     RecordSize: 100
# UNWIND-NEXT:     Flags: 0x0
# UNWIND-NEXT:     FunctionLength: 0x24
# UNWIND-NEXT:     PrologEndOffset: 0xC
# UNWIND-NEXT:     EstablisherFrameOffset: -8
# UNWIND-NEXT:     RequiredState: 0x3
# UNWIND-NEXT:     PrologCodes [
# UNWIND-NEXT:       Code {
# UNWIND-NEXT:         CodeOffset: 0x4
# UNWIND-NEXT:         Opcode: SET_CFA
# UNWIND-NEXT:         Register: $29 (29)
# UNWIND-NEXT:         Operand: 32
# UNWIND-NEXT:       }
# UNWIND-NEXT:       Code {
# UNWIND-NEXT:         CodeOffset: 0x8
# UNWIND-NEXT:         Opcode: SAVE_GPR
# UNWIND-NEXT:         Register: $31 (31)
# UNWIND-NEXT:         Operand: -4
# UNWIND-NEXT:       }
# UNWIND-NEXT:       Code {
# UNWIND-NEXT:         CodeOffset: 0xC
# UNWIND-NEXT:         Opcode: SAVE_FPR
# UNWIND-NEXT:         Register: $f20 (20)
# UNWIND-NEXT:         Operand: -16
# UNWIND-NEXT:       }
# UNWIND-NEXT:     ]
# UNWIND-NEXT:     EpilogScopes [
# UNWIND-NEXT:       EpilogScope {
# UNWIND-NEXT:         StartOffset: 0x14
# UNWIND-NEXT:         EndOffset: 0x24
# UNWIND-NEXT:         Codes [
# UNWIND-NEXT:           Code {
# UNWIND-NEXT:             CodeOffset: 0x4
# UNWIND-NEXT:             Opcode: SAME_FPR
# UNWIND-NEXT:             Register: $f20 (20)
# UNWIND-NEXT:           }
# UNWIND-NEXT:           Code {
# UNWIND-NEXT:             CodeOffset: 0x8
# UNWIND-NEXT:             Opcode: SAME_GPR
# UNWIND-NEXT:             Register: $31 (31)
# UNWIND-NEXT:           }
## The release in the jr delay slot takes effect after the slot.
# UNWIND-NEXT:           Code {
# UNWIND-NEXT:             CodeOffset: 0x10
# UNWIND-NEXT:             Opcode: SET_CFA
# UNWIND-NEXT:             Register: $29 (29)
# UNWIND-NEXT:             Operand: 0
# UNWIND-NEXT:           }
# UNWIND-NEXT:         ]
# UNWIND-NEXT:       }
# UNWIND-NEXT:     ]
# UNWIND-NEXT:   }
# UNWIND-NEXT: }

# SYM:      Name: @unwind.00
# SYM-NEXT: Value: 1
# SYM-NEXT: Section: IMAGE_SYM_ABSOLUTE (-1)

## .pdata holds image-relative {BeginAddress, UnwindData} pairs.
# RELOC:      Section ({{[0-9]+}}) .pdata {
# RELOC-NEXT:   0x0 IMAGE_REL_MIPS_REFWORDNB .text ({{[0-9]+}})
# RELOC-NEXT:   0x4 IMAGE_REL_MIPS_REFWORDNB .xdata ({{[0-9]+}})
# RELOC-NEXT: }

# OBJDUMP:      NTUW Function Table:
# OBJDUMP-NEXT:   Begin Address: .text
# OBJDUMP-NEXT:   Unwind Data: .xdata
# OBJDUMP-NEXT:   NTUW Unwind Info:
# OBJDUMP-NEXT:     Record Size: 100
# OBJDUMP-NEXT:     Flags: 0x0
# OBJDUMP-NEXT:     Function Length: 0x24
# OBJDUMP-NEXT:     Prologue End: 0xC
# OBJDUMP-NEXT:     Establisher Frame: CFA-8
# OBJDUMP-NEXT:     Required State: 0x3
# OBJDUMP-NEXT:     Prologue Codes:
# OBJDUMP-NEXT:       +0x0004: SET_CFA $sp, 32
# OBJDUMP-NEXT:       +0x0008: SAVE_GPR $ra, -4
# OBJDUMP-NEXT:       +0x000C: SAVE_FPR $f20, -16
# OBJDUMP-NEXT:     Epilogue Scopes:
# OBJDUMP-NEXT:       [0x14, 0x24):
# OBJDUMP-NEXT:         +0x0004: SAME_FPR $f20
# OBJDUMP-NEXT:         +0x0008: SAME_GPR $ra
# OBJDUMP-NEXT:         +0x0010: SET_CFA $sp, 0
