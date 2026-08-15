; RUN: llvm-armasm64 -I %S/Inputs %s %t.obj
; RUN: llvm-objdump -d %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
        INCLUDE include-order.inc
        END

; CHECK: add w0, w0, #0x1
