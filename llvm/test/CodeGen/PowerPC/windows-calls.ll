; Copyright (c) 2026 Ahmed ARIF

; REQUIRES: powerpc-registered-target
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O2 < %s | FileCheck %s
; NT images are rebased through base relocations; PIC does not apply to COFF.
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O2 -relocation-model=pic < %s | FileCheck %s
; RUN: llc -mtriple=powerpcle-pc-windows-msvc -O2 -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj -r %t.obj | FileCheck %s --check-prefix=OBJ

; Windows NT PowerPC import glue saves the caller's TOC with "stw r2,4(r1)".
; A call that may reach glue is followed by a nop carrying an IFGLUE
; relocation, which the linker replaces with "lwz r2,4(r1)". The return
; address is therefore saved at 8(entry SP), outside the glue's TOC word.

; CHECK-LABEL: ..calls:
; CHECK:       mflr 0
; CHECK-NEXT:  stwu 1, -64(1)
; CHECK-NEXT:  stw 0, 72(1)
; A call to a function defined in this module shares its TOC.
; CHECK:       bl ..local
; CHECK-NEXT:  bl ..external
; CHECK-NEXT:  .znop ..external
; A dllimport call loads the imported descriptor from the IAT slot and calls
; through it, saving and restoring the caller's TOC at 4(r1).
; CHECK-NEXT:  lis [[IMP:[0-9]+]], __imp_imported@ha
; CHECK-NEXT:  lwz [[DESC:[0-9]+]], __imp_imported@l([[IMP]])
; CHECK-NEXT:  stw 2, 4(1)
; CHECK-NEXT:  lwz [[ENTRY:[0-9]+]], 0([[DESC]])
; CHECK-NEXT:  lwz 2, 4([[DESC]])
; CHECK-NEXT:  mtctr [[ENTRY]]
; CHECK-NEXT:  bctrl
; CHECK-NEXT:  lwz 2, 4(1)
; CHECK:       lis [[DATA:[0-9]+]], __imp_imported_data@ha
; CHECK-NEXT:  lwz [[DATA]], __imp_imported_data@l([[DATA]])
; CHECK-NEXT:  lwz {{[0-9]+}}, 0([[DATA]])
; CHECK:       lwz 0, 72(1)
; CHECK:       mtlr 0

; The address of a dllimport function is the descriptor in the other image.
; CHECK-LABEL: ..addr_imported:
; CHECK:       lis 3, __imp_imported@ha
; CHECK-NEXT:  lwz 3, __imp_imported@l(3)

; CHECK-LABEL: ..callp:
; CHECK:       stw 2, 4(1)
; CHECK:       bctrl
; CHECK-NEXT:  lwz 2, 4(1)

; The call to the local function resolves within the section.
; OBJ:      Section ({{[0-9]+}}) .text {
; OBJ-NEXT:   IMAGE_REL_PPC_REL24 ..external
; OBJ-NEXT:   IMAGE_REL_PPC_IFGLUE ..external
; OBJ-NEXT:   IMAGE_REL_PPC_REFHI __imp_imported
; OBJ-NEXT:   IMAGE_REL_PPC_PAIR
; OBJ-NEXT:   IMAGE_REL_PPC_REFLO __imp_imported
; OBJ-NEXT:   IMAGE_REL_PPC_REFHI __imp_imported_data
; OBJ-NEXT:   IMAGE_REL_PPC_PAIR
; OBJ-NEXT:   IMAGE_REL_PPC_REFLO __imp_imported_data

@imported_data = external dllimport global i32

declare i32 @external(i32)
declare dllimport i32 @imported(i32)

define internal i32 @local(i32 %x) noinline {
  %r = add i32 %x, 1
  ret i32 %r
}

define i32 @calls(i32 %x) {
  %a = call i32 @local(i32 %x)
  %b = call i32 @external(i32 %a)
  %c = call i32 @imported(i32 %b)
  %d = load i32, ptr @imported_data
  %e = add i32 %c, %d
  ret i32 %e
}

define ptr @addr_imported() {
  ret ptr @imported
}

define i32 @callp(ptr %f) {
  %r = call i32 %f(i32 1)
  ret i32 %r
}
