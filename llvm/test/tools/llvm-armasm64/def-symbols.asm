; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
        MAP 0
slot    FIELD 4
route   ROUT
        IF :DEF:slot :LAND: :DEF:route
        DCB 1
        ENDIF
        END

; CHECK:      Contents of section .data:
; CHECK-NEXT: 0000 01
