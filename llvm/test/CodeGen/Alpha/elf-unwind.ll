; RUN: llc -mtriple=alpha-unknown-linux-gnu -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=alpha-unknown-linux-gnu -filetype=obj %s -o - | llvm-readobj --sections --relocations --unwind - 2>&1 | FileCheck %s --check-prefix=OBJ

; ASM: .cfi_startproc
; ASM: lda $30,-48($30)
; ASM-NEXT: .cfi_def_cfa_offset 48
; ASM: stq $9,40($30)
; ASM-NEXT: .cfi_offset $9, -8
; ASM: .cfi_endproc
; ASM-LABEL: frame_pointer:
; ASM: .cfi_offset $15, -32
; ASM-NEXT: .cfi_def_cfa $15, 32

; OBJ: Name: .eh_frame
; OBJ-NOT: warning:
; OBJ: Name: .rela.eh_frame
; OBJ: R_ALPHA_SREL32 .text 0x0
; OBJ: return_address_register: 26
; OBJ: DW_CFA_def_cfa: reg30 +0
; OBJ: DW_CFA_def_cfa_offset: +48
; OBJ: DW_CFA_offset: reg9 -8
; OBJ: DW_CFA_offset: reg15 -32
; OBJ: DW_CFA_def_cfa: reg15 +32

target triple = "alpha-unknown-linux-gnu"

declare void @callee()
declare void @use(ptr)

define void @unwindable() uwtable {
entry:
  %slot = alloca [32 x i8], align 16
  call void @callee()
  ret void
}

define void @frame_pointer(i64 %count) uwtable {
entry:
  %slot = alloca i8, i64 %count, align 16
  call void @use(ptr %slot)
  ret void
}
