; RUN: llvm-armasm64 -I "%t/missing;%S/Inputs" %s %t.obj
; RUN: llvm-objdump -d %t.obj | FileCheck %s
; RUN: cd %S/Inputs && llvm-armasm64 %s %t-cwd.obj
; RUN: llvm-objdump -d %t-cwd.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
step    EQU 2
        INCLUDE include-one.inc
        END

; CHECK:      add w0, w0, #0x2
; CHECK-NEXT: add w0, w0, #0x3
