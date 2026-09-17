; RUN: llc -mtriple=riscv64-w64-windows-gnu < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=riscv64-w64-windows-gnu -filetype=obj < %s | llvm-readobj --relocations - | FileCheck %s --check-prefix=RELOC

@tls_var = thread_local global i32 0, align 4

define ptr @get_tls_ptr() {
; ASM:          .secrel32 tls_var
; ASM-LABEL: get_tls_ptr:
; ASM:          auipc [[INDEX_ADDR:a[0-9]+]], %pcrel_hi(_tls_index)
; ASM-NEXT:     lwu [[INDEX:a[0-9]+]], %pcrel_lo({{.*}})([[INDEX_ADDR]])
; ASM-NEXT:     ld [[ARRAY:a[0-9]+]], 88(tp)
; ASM-NEXT:     slli [[INDEX]], [[INDEX]], 3
; ASM-NEXT:     add [[SLOT:a[0-9]+]], [[ARRAY]], [[INDEX]]
; ASM:          ld [[BLOCK:a[0-9]+]], 0([[SLOT]])
; ASM:          lwu [[OFFSET:a[0-9]+]], %pcrel_lo({{.*}})({{a[0-9]+}})
; ASM-NEXT:     add a0, [[BLOCK]], [[OFFSET]]
  ret ptr @tls_var
}

; RELOC: IMAGE_REL_RISCV_SECREL tls_var
