; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
        DCD ?standalone, ?proc, ?later
standalone
        DCB 1

        AREA |.text|, CODE, READONLY
proc    PROC
        nop
        ret
        ENDP

        AREA |.data|, DATA, READWRITE
later   DCB 1, 2, 3
        END

; CHECK:      Contents of section .data:
; CHECK-NEXT: 0000 00000000 00000000 03000000 01010203
