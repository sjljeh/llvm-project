; REQUIRES: powerpc-registered-target
; RUN: llc -mtriple=powerpcle-pc-windows-msvc < %s | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --symbols --relocations %t.obj | FileCheck %s --check-prefix=OBJ

; Windows PowerPC function pointers address two-word descriptors containing the
; code address and TOC value. Direct calls name the code-entry symbol.

; ASM:      .globl ..callee
; ASM:      .globl callee
; ASM:      .section .rdata,"dr"
; ASM:      callee:
; ASM-NEXT: .long ..callee
; ASM-NEXT: .long .toc
; ASM:      ..callee:

; ASM-LABEL: ..caller:
; ASM:      bl ..callee
; ASM:      bl ..external

; An indirect call must consume only the two words present in a Windows PPC
; descriptor. In particular, it must not read the following word as the
; ELFv1/AIX environment pointer.
; ASM-LABEL: ..callp:
; ASM-NOT:  lwz {{[0-9]+}}, 8(3)
; ASM:      lwz [[ENTRY:[0-9]+]], 0(3)
; ASM-NOT:  lwz {{[0-9]+}}, 8(3)
; ASM:      lwz 2, 4(3)
; ASM-NOT:  lwz {{[0-9]+}}, 8(3)
; ASM:      mtctr [[ENTRY]]
; ASM-NOT:  lwz {{[0-9]+}}, 8(3)
; ASM:      bctrl

; OBJ:      Section ({{[0-9]+}}) .text {
; OBJ:      IMAGE_REL_PPC_REL24 ..external
; OBJ:      Section ({{[0-9]+}}) .rdata {
; OBJ:      IMAGE_REL_PPC_ADDR32 ..callee
; OBJ-NEXT: IMAGE_REL_PPC_ADDR32 .toc
; OBJ:      IMAGE_REL_PPC_ADDR32 ..caller
; OBJ-NEXT: IMAGE_REL_PPC_ADDR32 .toc
; OBJ:      IMAGE_REL_PPC_ADDR32 ..callp
; OBJ-NEXT: IMAGE_REL_PPC_ADDR32 .toc
; OBJ:      Name: ..callee
; OBJ:      Name: callee
; OBJ:      Name: ..external

source_filename = "windows-function-descriptors.c"
target datalayout = "e-m:e-p:32:32-Fn32-i64:64-n32"
target triple = "powerpcle-pc-windows-msvc"

define i32 @callee(i32 %x) {
entry:
  %add = add nsw i32 %x, 1
  ret i32 %add
}

declare i32 @external(i32)

define i32 @caller(i32 %x) {
entry:
  %a = call i32 @callee(i32 %x)
  %b = call i32 @external(i32 %x)
  %sum = add nsw i32 %a, %b
  ret i32 %sum
}

define i32 @callp(ptr %fn, i32 %x) {
entry:
  %result = call i32 %fn(i32 %x)
  ret i32 %result
}
