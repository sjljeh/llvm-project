; REQUIRES: mips

; RUN: llvm-as %s -o %t.obj
; RUN: lld-link /out:%t.exe /entry:main /subsystem:native /machine:mips %t.obj

target triple = "mipsel-pc-windows-msvc"
target datalayout = "e-m:m-p:32:32-i8:8:32-i16:16:32-i64:64-n32-S64"

define i32 @main() {
  ret i32 0
}
