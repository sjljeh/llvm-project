; Each area keeps its ldr= literal pool across AREA switches until LTORG or
; END, as Microsoft armasm64 does; END places every pending pool at the end of
; its area, aligned to 4 bytes.
; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -d %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
first   ldr x0, =0x1122334455667788
        ldr w1, =0x12345678
        AREA |second|, CODE, READONLY
        ldr w2, =0x0badf00d
        AREA |.text|, CODE, READONLY
        ldr x3, =0x8877665544332211
        ret
        AREA |second|, CODE, READONLY
        ret
        END

; CHECK-LABEL: <first>:
; CHECK-NEXT:  0: 580000c0 ldr x0, 0x18 <first+0x18>
; CHECK-NEXT:  4: 180000e1 ldr w1, 0x20 <first+0x20>
; CHECK-NEXT:  8: 58000043 ldr x3, 0x10 <first+0x10>
; CHECK-NEXT:  c: d65f03c0 ret
; CHECK-NEXT: 10: 44332211
; CHECK-NEXT: 14: 88776655
; CHECK-NEXT: 18: 55667788
; CHECK-NEXT: 1c: 11223344
; CHECK-NEXT: 20: 12345678
; CHECK-LABEL: <second>:
; CHECK-NEXT:  0: 18000042 ldr w2, 0x8 <second+0x8>
; CHECK-NEXT:  4: d65f03c0 ret
; CHECK-NEXT:  8: 0badf00d
