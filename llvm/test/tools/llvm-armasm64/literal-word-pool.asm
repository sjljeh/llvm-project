; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
        LDR w0, =0x12345678
        LTORG
        ret
        END

; CHECK:      Contents of section .text:
; CHECK-NEXT: 0000 20000018 78563412 c0035fd6
; CHECK:      0: 18000020      ldr w0, 0x4 <.text+0x4>
; CHECK-NEXT: 4: 12345678
; CHECK-NEXT: 8: d65f03c0      ret
