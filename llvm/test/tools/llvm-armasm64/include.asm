; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -d %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
        EXPORT include_test
step    EQU 2
include_test PROC
        mov w0, #1
        INCLUDE Inputs/include-one.inc
        add w0, w0, #4
        ret
        ENDP
        END

        not_an_instruction

; CHECK:      mov w0, #0x1
; CHECK-NEXT: add w0, w0, #0x2
; CHECK-NEXT: add w0, w0, #0x3
; CHECK-NEXT: add w0, w0, #0x4
; CHECK-NEXT: ret
