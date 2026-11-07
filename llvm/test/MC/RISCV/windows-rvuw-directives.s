# RUN: llvm-mc -triple riscv64-pc-windows-msvc -mattr=+d,+v %s | FileCheck %s --check-prefix=ASM
# RUN: llvm-mc -triple riscv64-pc-windows-msvc -mattr=+d,+v -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=OBJ
# RUN: llvm-objdump -u %t.obj | FileCheck %s --check-prefix=DUMP
# RUN: not llvm-mc -triple riscv64-pc-windows-msvc -mattr=+d,+v -filetype=obj -defsym=ERR=1 %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=ERR
#
# Every RVUW directive, through the assembly printer, the encoder,
# and both dumpers.

.ifndef ERR
.text
.seh_proc rules
rules:
  .seh_establisher s1, 16
  addi sp, sp, -2032
  .seh_set_cfa sp, 2032
  sd ra, 2024(sp)
  .seh_save_gpr ra, -8
  fsd fs0, 2016(sp)
  .seh_save_fpr fs0, -16
  .seh_save_fcsr -20
  mv s2, ra
  .seh_gpr_from_gpr s3, s2
  .seh_fpr_from_fpr fs1, fs2
  csrr t0, vlenb
  sub sp, sp, t0
  .seh_set_cfa_scaled sp, 2032, 1
  .seh_save_vr v8, -2032, -1
  nop
  .seh_set_cfa sp, 2000000
  .seh_endprologue
  nop
  .seh_startepilogue
  .seh_same_vr v8
  .seh_same_fpr fs0
  .seh_same_fcsr
  .seh_same_gpr ra
  ret
  .seh_endepilogue
.seh_endproc

.seh_proc frames
frames:
  .seh_return_from t0, -4
  addi sp, sp, -800
  .seh_set_cfa sp, 800
  .seh_context -800
  nop
  .seh_trap_frame -400
  nop
  .seh_machine_frame -16
  .seh_endprologue
  nop
.seh_endproc
.endif

# ASM:      .seh_establisher x9, 16
# ASM:      .seh_set_cfa x2, 2032
# ASM:      .seh_save_gpr x1, -8
# ASM:      .seh_save_fpr f8, -16
# ASM:      .seh_save_fcsr -20
# ASM:      .seh_gpr_from_gpr x19, x18
# ASM:      .seh_fpr_from_fpr f9, f18
# ASM:      .seh_set_cfa_scaled x2, 2032, 1
# ASM:      .seh_save_vr v8, -2032, -1
# ASM:      .seh_set_cfa x2, 2000000
# ASM:      .seh_same_vr v8
# ASM:      .seh_same_fpr f8
# ASM:      .seh_same_fcsr
# ASM:      .seh_same_gpr x1
# ASM:      .seh_return_from x5, -4
# ASM:      .seh_context -800
# ASM:      .seh_trap_frame -400
# ASM:      .seh_machine_frame -16

# OBJ-NOT:  ValidationError
# OBJ:      RequiredState: 0x7
# OBJ:      EstablisherRegister: x9 (9)
# OBJ:      Opcode: SET_CFA_SCALED
# OBJ-NEXT: Register: x2 (2)
# OBJ-NEXT: Operand: 2032
# OBJ-NEXT: VLENBMultiplier: 1
# OBJ:      Opcode: SAVE_VR
# OBJ-NEXT: Register: v8 (8)
# OBJ-NEXT: Operand: -2032
# OBJ-NEXT: VLENBMultiplier: -1
# OBJ:      Opcode: SET_CFA_LARGE
# OBJ-NEXT: Register: x2 (2)
# OBJ-NEXT: Operand: 2000000
# OBJ:      RequiredState: 0x3
# OBJ:      Opcode: RETURN_FROM_GPR
# OBJ-NEXT: Register: x5 (5)
# OBJ-NEXT: Operand: -4
# OBJ:      Opcode: CONTEXT
# OBJ-NEXT: Operand: -800
# OBJ:      Opcode: TRAP_FRAME
# OBJ-NEXT: Operand: -400
# OBJ:      Opcode: MACHINE_FRAME
# OBJ-NEXT: Operand: -16
# OBJ-NOT:  ValidationError

# DUMP:     SAVE_FPR f8, -16
# DUMP:     GPR_FROM_GPR x19, x18
# DUMP:     FPR_FROM_FPR f9, f18
# DUMP:     SET_CFA_SCALED x2, 2032 + 1 * vlenb
# DUMP:     SAVE_VR v8, -2032 + -1 * vlenb
# DUMP:     SAME_VR v8
# DUMP:     RETURN_FROM_GPR x5, -4

.ifdef ERR
.seh_proc errors
errors:
# ERR: [[#@LINE+1]]:3: error: SET_CFA names an invalid base register
  .seh_set_cfa tp, 16
# ERR: [[#@LINE+1]]:3: error: SAVE_GPR names zero or sp
  .seh_save_gpr sp, -8
# ERR: [[#@LINE+1]]:3: error: SET_CFA_SCALED needs a nonzero VLENB multiplier
  .seh_set_cfa_scaled sp, 16, 0
# ERR: [[#@LINE+1]]:3: error: RETURN_FROM_GPR has an invalid register or an odd adjustment
  .seh_return_from t0, -3
# ERR: [[#@LINE+1]]:3: error: the establisher must be sp or a nonvolatile register
  .seh_establisher t0, 0
# ERR: [[#@LINE+1]]:17: error: expected a floating-point register
  .seh_save_fpr s0, -8
# ERR: [[#@LINE+1]]:3: error: frame rule has a register or a misaligned offset
  .seh_context -4
.seh_endproc
.endif
