; ADRL assembles as adr.w with the low 12 bits of the offset, then add.w (or
; sub.w, for a target behind the PC) with the rest, as Microsoft armasm does.
; RUN: llvm-armasm %s %t.obj
; RUN: llvm-objdump -d --triple=thumbv7-windows %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
back    nop
        SPACE 6000
f       PROC
        adrl r0, near
        adrl r1, back
        adrl r2, vfar
        adrl r3, here
        bx lr
near    nop
        SPACE 60000
vfar    nop
        ENDP
here
        adrl r4, here
        END

; CHECK-LABEL: <f>:
; CHECK-NEXT:   1772: f20f 0020 adr.w r0, #32
; CHECK-NEXT:   1776: f100 0000 add.w r0, r0, #0x0
; CHECK-NEXT:   177a: f2af 717c adr.w r1, #-1916
; CHECK-NEXT:   177e: f5a1 5180 sub.w r1, r1, #0x1000
; CHECK-NEXT:   1782: f60f 2272 adr.w r2, #2674
; CHECK-NEXT:   1786: f502 4260 add.w r2, r2, #0xe000
; CHECK-NEXT:   178a: f60f 236c adr.w r3, #2668
; CHECK-NEXT:   178e: f503 4360 add.w r3, r3, #0xe000
; CHECK-LABEL: <here>:
; CHECK-NEXT:  101f8: f2af 0404 adr.w r4, #-4
; CHECK-NEXT:  101fc: f1a4 0400 sub.w r4, r4, #0x0
