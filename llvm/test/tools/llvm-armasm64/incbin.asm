; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
        INCBIN Inputs/incbin.dat
        END

; CHECK:      Contents of section .data:
; CHECK-NEXT: 0000 4142430a
