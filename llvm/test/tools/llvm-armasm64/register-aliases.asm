; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -d %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY

        fmov d5, Forward
Forward     DN d9
Double      DN d4
DoubleCopy  DN Double
Double      DN 2 + 3
Quad        QN 6
QuadCopy    QN Quad
Quad        QN q7
Single      SN (4 + 4)
Case        DN d12
case        DN d13
|Quoted|    DN d14

        fmov d0, Double
        fmov d1, DoubleCopy
        fmov d2, Case
        fmov d3, case
        str Quad, [sp]
        str QuadCopy, [sp, #16]
        movi QuadCopy.16b, #1
        fmov s0, Single
        fmov d4, |Quoted|
        movi DoubleCopy.8b, #2
d0      DN d15
        fmov d0, Double
        fmov D0, Double
        END

; CHECK:      0: 1e604125      fmov d5, d9
; CHECK-NEXT: 4: 1e6040af      fmov d15, d5
; CHECK-NEXT: 8: 1e604081      fmov d1, d4
; CHECK-NEXT: c: 1e604182      fmov d2, d12
; CHECK-NEXT: 10: 1e6041a3     fmov d3, d13
; CHECK-NEXT: 14: 3d8003e7     str q7, [sp]
; CHECK-NEXT: 18: 3d8007e6     str q6, [sp, #0x10]
; CHECK-NEXT: 1c: 4f00e426     movi v6.16b, #0x1
; CHECK-NEXT: 20: 1e204100     fmov s0, s8
; CHECK-NEXT: 24: 1e6041c4     fmov d4, d14
; CHECK-NEXT: 28: 0f00e444     movi v4.8b, #0x2
; CHECK-NEXT: 2c: 1e6040af     fmov d15, d5
; CHECK-NEXT: 30: 1e6040a0     fmov d0, d5
