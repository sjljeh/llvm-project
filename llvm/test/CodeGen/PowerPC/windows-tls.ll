; Copyright (c) 2026 Ahmed ARIF

; REQUIRES: powerpc-registered-target
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O2 < %s | FileCheck %s
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O2 -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj -r %t.obj | FileCheck %s --check-prefix=OBJ

; Windows NT keeps the TEB in r13. TEB->ThreadLocalStoragePointer (offset
; 0x2C) holds the image TLS blocks indexed by the loader-assigned _tls_index,
; and the variable lives at its section-relative offset within that block.

; CHECK-LABEL: ..tls_read:
; CHECK-DAG:   lis [[IDXHI:[0-9]+]], _tls_index@ha
; CHECK-DAG:   lwz [[IDX:[0-9]+]], _tls_index@l([[IDXHI]])
; CHECK-DAG:   lwz [[ARRAY:[0-9]+]], 44(13)
; CHECK-DAG:   slwi [[SLOT:[0-9]+]], [[IDX]], 2
; CHECK-DAG:   lwzx [[BLOCK:[0-9]+]], [[ARRAY]], [[SLOT]]
; CHECK-DAG:   lis{{[[:space:]]+}}[[OFFHI:[0-9]+]], tlsvar@secrel@ha
; CHECK-DAG:   li{{[[:space:]]+}}[[OFFLO:[0-9]+]], tlsvar@secrel@l
; CHECK:       blr

; CHECK:       .section .tls$,"dw"
; CHECK:       tlsvar:

; OBJ:      Section ({{[0-9]+}}) .text {
; OBJ:        IMAGE_REL_PPC_REFHI _tls_index
; OBJ-NEXT:   IMAGE_REL_PPC_PAIR
; OBJ-NEXT:   IMAGE_REL_PPC_REFLO _tls_index
; OBJ-NEXT:   IMAGE_REL_PPC_SECRELHI tlsvar
; OBJ-NEXT:   IMAGE_REL_PPC_PAIR
; OBJ-NEXT:   IMAGE_REL_PPC_SECRELLO tlsvar

@tlsvar = thread_local global i32 7

define i32 @tls_read() {
  %p = call ptr @llvm.threadlocal.address.p0(ptr @tlsvar)
  %v = load i32, ptr %p
  ret i32 %v
}

declare ptr @llvm.threadlocal.address.p0(ptr)
