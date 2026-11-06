; RUN: llvm-armasm %s %t.obj
; RUN: llvm-objdump -dr --triple=thumbv7-windows %t.obj | FileCheck %s

; Matches Microsoft armasm: LDR = uses MOV.W, MVN or MOVW when one
; instruction can produce the value, and otherwise loads from a literal pool.
; Each area keeps one pool, placed at its end; equal values share an entry.

        AREA |.text|, CODE, READONLY
        IMPORT ext
f       PROC
        ldr r0, =5
        ldr r1, =0x1234
        ldr r2, =0x12345678
        ldr r3, =ext
        ldr r4, =0xffffffff
        ldr r5, =-2
        ldr r6, =0x12345678
        ldr r7, =ext+4
        ldr.w r8, =0x55555555
        bx lr
        ENDP
g       PROC
        ldr r0, =0x87654321
        bx lr
        ENDP
        AREA |.data|, DATA
d       DCD 1
        AREA |.text|, CODE, READONLY
h       PROC
        ldr r0, =0xabcdef01
        bx lr
        ENDP
        END

; CHECK:      0: f04f 0005 mov.w r0, #0x5
; CHECK-NEXT: 4: f241 2134 movw r1, #0x1234
; CHECK-NEXT: 8: 4a07 ldr r2, [pc, #0x1c]
; CHECK-NEXT: a: 4b08 ldr r3, [pc, #0x20]
; CHECK-NEXT: c: f04f 34ff mov.w r4, #0xffffffff
; CHECK-NEXT: 10: f06f 0501 mvn r5, #0x1
; CHECK-NEXT: 14: 4e04 ldr r6, [pc, #0x10]
; CHECK-NEXT: 16: 4f06 ldr r7, [pc, #0x18]
; CHECK-NEXT: 18: f04f 3855 mov.w r8, #0x55555555
; CHECK-NEXT: 1c: 4770 bx lr
; CHECK:      1e: 4805 ldr r0, [pc, #0x14]
; CHECK:      22: 4805 ldr r0, [pc, #0x14]
; CHECK-NEXT: 24: 4770 bx lr
; CHECK-NEXT: 26: 0000
; CHECK-NEXT: 28: 5678
; CHECK-NEXT: 2a: 1234
; CHECK-NEXT: 2c: 0000
; CHECK-NEXT: 0000002c: IMAGE_REL_ARM_ADDR32 ext
; CHECK-NEXT: 2e: 0000
; CHECK-NEXT: 30: 0004
; CHECK-NEXT: 00000030: IMAGE_REL_ARM_ADDR32 ext
; CHECK-NEXT: 32: 0000
; CHECK-NEXT: 34: 4321
; CHECK-NEXT: 36: 8765
; CHECK-NEXT: 38: ef01 abcd
