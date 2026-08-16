; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

value   * 7
        AREA |.data|, DATA, READWRITE
        IF :DEF:value :LAND: value = 7
        DCD value
        ENDIF
        END

; CHECK:      Contents of section .data:
; CHECK-NEXT: 0000 07000000
