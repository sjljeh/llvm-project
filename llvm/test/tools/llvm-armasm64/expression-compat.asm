; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |logical_not|, DATA, READWRITE
        IF !:DEF:missing
        DCB 1
        ELSE
        DCB 2
        ENDIF

        AREA |signed_arithmetic|, DATA, READWRITE
        GBLA value
value   SETA -5 / 2
        DCD value
value   SETA -5 :MOD: 2
        DCD value

        AREA |symbol_size|, DATA, READWRITE
datum   DCD 1
        DCD ?datum

        AREA |register_relative|, DATA, READWRITE
        MAP 3, x0
slot    FIELD 1
        DCB :BASE:slot, :INDEX:slot
        DCB :BASE:(slot + 2), :INDEX:(slot + 2)
        END

; CHECK:      Contents of section logical_not:
; CHECK-NEXT: 0000 01
; CHECK:      Contents of section signed_arithmetic:
; CHECK-NEXT: 0000 feffffff ffffffff
; CHECK:      Contents of section symbol_size:
; CHECK-NEXT: 0000 01000000 04000000
; CHECK:      Contents of section register_relative:
; CHECK-NEXT: 0000 22002202
