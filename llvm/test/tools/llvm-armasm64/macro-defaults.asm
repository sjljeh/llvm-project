; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
        MACRO
        EMIT $first,$value=7,$last
        DCB $first,$value,$last
        MEND

        EMIT 1,|,3
        EMIT 4,5,6
        END

; CHECK:      Contents of section .data:
; CHECK-NEXT: 0000 01070304 0506
