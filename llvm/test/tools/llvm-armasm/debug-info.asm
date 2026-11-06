; RUN: llvm-armasm -g %s %t.obj
; RUN: llvm-readobj --codeview %t.obj | FileCheck %s

; CodeView for Thumb-2 sources records the ARMNT CPU and a line table for
; each code area.

        AREA |.text|, CODE, READONLY
        EXPORT f
f       PROC
        movs r0, #1
        bx lr
        ENDP
        END

; CHECK:      ObjNameSym {
; CHECK:        ObjectName: {{.*}}.obj
; CHECK:      Compile3Sym {
; CHECK-NEXT:   Kind: S_COMPILE3
; CHECK-NEXT:   Language: Masm
; CHECK-NEXT:   Flags [ (0x0)
; CHECK-NEXT:   ]
; CHECK-NEXT:   Machine: ARMNT
; CHECK:        VersionName: LLVM ARMASM Assembler
; CHECK:      GlobalProcSym {
; CHECK:        CodeSize: 0x4
; CHECK:        DisplayName: f
; CHECK:      FunctionLineTable [
; CHECK:        LinkageName: .text
; CHECK:        LineNumberStart: 10
; CHECK:        LineNumberStart: 11
