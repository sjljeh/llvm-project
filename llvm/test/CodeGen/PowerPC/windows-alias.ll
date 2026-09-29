; Copyright (c) 2026 Ahmed ARIF

; RUN: llc -mtriple=powerpcle-pc-windows-gnu < %s | FileCheck %s

; A function alias names the descriptor. Calls reference the "..name" code
; entry, so the alias also needs a code entry alias.

define void @base() {
  ret void
}

@alias = alias void (), ptr @base
@weak_alias = weak alias void (), ptr @base
@alias_of_alias = alias void (), ptr @alias
@data = global i32 0
@data_alias = alias i32, ptr @data

; CHECK:      .globl alias
; CHECK:      alias = base
; CHECK-NEXT: .globl ..alias
; CHECK-NEXT: ..alias = ..base
; CHECK:      .weak weak_alias
; CHECK:      weak_alias = base
; CHECK-NEXT: .weak ..weak_alias
; CHECK-NEXT: ..weak_alias = ..base
; CHECK:      alias_of_alias = alias
; CHECK-NEXT: .globl ..alias_of_alias
; CHECK-NEXT: ..alias_of_alias = ..alias
; CHECK:      data_alias = data
; CHECK-NOT:  ..data_alias

; A call to an extern_weak function goes through its "..name" code entry,
; which is weak like the descriptor.
declare extern_weak void @weak_decl()

define void @call_weak() {
  call void @weak_decl()
  ret void
}

; CHECK: .weak ..weak_decl
