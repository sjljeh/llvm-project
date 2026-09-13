; RUN: llc -mtriple=riscv64-w64-windows-gnu < %s | FileCheck %s

; PE images live above the first 2 GiB by default, so COFF targets default to
; the medium code model and address globals PC-relatively.

@var = global i32 0
@imported = external dllimport global i32
@weak_var = extern_weak global i32
declare void @callee()
declare dllimport void @imported_callee()

define i32 @load_var() {
; CHECK-LABEL: load_var:
; CHECK:         .Lpcrel_hi0:
; CHECK-NEXT:    auipc a0, %pcrel_hi(var)
; CHECK-NEXT:    lw a0, %pcrel_lo(.Lpcrel_hi0)(a0)
; CHECK-NEXT:    ret
  %v = load i32, ptr @var
  ret i32 %v
}

define i32 @load_imported() {
; CHECK-LABEL: load_imported:
; CHECK:         .Lpcrel_hi1:
; CHECK-NEXT:    auipc a0, %pcrel_hi(__imp_imported)
; CHECK-NEXT:    ld a0, %pcrel_lo(.Lpcrel_hi1)(a0)
; CHECK-NEXT:    lw a0, 0(a0)
; CHECK-NEXT:    ret
  %v = load i32, ptr @imported
  ret i32 %v
}

define ptr @weak_address() {
; CHECK-LABEL: weak_address:
; CHECK:         .Lpcrel_hi2:
; CHECK-NEXT:    auipc a0, %pcrel_hi(.refptr.weak_var)
; CHECK-NEXT:    ld a0, %pcrel_lo(.Lpcrel_hi2)(a0)
; CHECK-NEXT:    ret
  ret ptr @weak_var
}

; The offset applies to the loaded pointer, never to the stub cell itself.
define ptr @weak_address_offset() {
; CHECK-LABEL: weak_address_offset:
; CHECK:         .Lpcrel_hi3:
; CHECK-NEXT:    auipc a0, %pcrel_hi(.refptr.weak_var)
; CHECK-NEXT:    ld a0, %pcrel_lo(.Lpcrel_hi3)(a0)
; CHECK-NEXT:    addi a0, a0, 8
; CHECK-NEXT:    ret
  ret ptr getelementptr (i8, ptr @weak_var, i64 8)
}

define i32 @load_imported_offset() {
; CHECK-LABEL: load_imported_offset:
; CHECK:         .Lpcrel_hi4:
; CHECK-NEXT:    auipc a0, %pcrel_hi(__imp_imported)
; CHECK-NEXT:    ld a0, %pcrel_lo(.Lpcrel_hi4)(a0)
; CHECK-NEXT:    lw a0, 4(a0)
; CHECK-NEXT:    ret
  %v = load i32, ptr getelementptr (i8, ptr @imported, i64 4)
  ret i32 %v
}

define void @calls() {
; CHECK-LABEL: calls:
; CHECK:         call callee
; CHECK:         call imported_callee
  call void @callee()
  call void @imported_callee()
  ret void
}

; CHECK:      .section .rdata$.refptr.weak_var,"dr",discard,.refptr.weak_var
; CHECK:      .refptr.weak_var:
; CHECK-NEXT: .quad weak_var
