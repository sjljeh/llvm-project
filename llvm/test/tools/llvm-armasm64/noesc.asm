; RUN: llvm-armasm64 %s %t-default.obj
; RUN: llvm-objdump -s %t-default.obj | FileCheck %s --check-prefix=DEFAULT
; RUN: llvm-armasm64 -noesc %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=NOESC
; The object path is recorded in .debug$S, so both runs write the same path.
; RUN: mv %t.obj %t-noesc.obj
; RUN: llvm-armasm64 -noe %s %t.obj
; RUN: cmp %t-noesc.obj %t.obj

        AREA |.data|, DATA, READWRITE
        DCB "a\nb\tc\\d", "e""f", "$$"
        END

; DEFAULT: Contents of section .data:
; DEFAULT-NEXT: 0000 610a6209 635c6465 226624
; NOESC: Contents of section .data:
; NOESC-NEXT: 0000 615c6e62 5c74635c 5c646522 6624
