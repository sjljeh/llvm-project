; armasm leaves BL and wide branches to a symbol without an addend to the
; linker, even within a section (whether .W is written or the target is out
; of narrow range), resolves narrow branches and BLX where it can, and writes
; a BLX it leaves to the linker as BL. A branch to any relocatable target
; starts narrow. Checked against Microsoft armasm.
; RUN: llvm-armasm %s %t.obj
; RUN: llvm-objdump -dr --triple=thumbv7-windows %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
        IMPORT ext
back    nop
        bl back
        b back
        blx back
        b.w back
        bne.w back
        it eq
        beq.w back
        bl back+2
        b far
        bne far
        bl %f1
1       nop
        blx ext
near    nop
        b near+2
        bne near+2
        SPACE 3000
far     nop
        bx lr
        AREA |.text2|, CODE, READONLY
        blx back
        END

; CHECK-LABEL: <back>:
; CHECK-NEXT:    0: bf00      nop
; CHECK-NEXT:    2: f000 f800 bl
; CHECK-NEXT:       IMAGE_REL_ARM_BLX23T back
; CHECK-NEXT:    6: e7fb      b 0x0 <back>
; CHECK-NEXT:    8: f7ff effa blx 0x0 <back>
; CHECK-NEXT:    c: f000 b800 b.w
; CHECK-NEXT:       IMAGE_REL_ARM_BRANCH24T back
; CHECK-NEXT:   10: f040 8000 bne.w
; CHECK-NEXT:       IMAGE_REL_ARM_BRANCH20T back
; CHECK-NEXT:   14: bf08      it eq
; CHECK-NEXT:   16: f000 b800 beq.w
; CHECK-NEXT:       IMAGE_REL_ARM_BRANCH24T back
; CHECK-NEXT:   1a: f7ff fff2 bl 0x2 <back+0x2>
; CHECK-NEXT:   1e: f000 b800 b.w
; CHECK-NEXT:       IMAGE_REL_ARM_BRANCH24T far
; CHECK-NEXT:   22: f040 8000 bne.w
; CHECK-NEXT:       IMAGE_REL_ARM_BRANCH20T far
; CHECK-NEXT:   26: f000 f800 bl
; CHECK-NEXT:       IMAGE_REL_ARM_BLX23T _lc001_000023_
; CHECK-LABEL: <_lc001_000023_>:
; CHECK-NEXT:   2a: bf00      nop
; CHECK-NEXT:   2c: f000 f800 bl
; CHECK-NEXT:       IMAGE_REL_ARM_BLX23T ext
; CHECK-LABEL: <near>:
; CHECK-NEXT:   30: bf00      nop
; CHECK-NEXT:   32: e7fe      b 0x32 <near+0x2>
; CHECK-NEXT:   34: d1fd      bne 0x32 <near+0x2>
; CHECK-LABEL: <.text2>:
; CHECK-NEXT:    0: f000 f800 bl
; CHECK-NEXT:       IMAGE_REL_ARM_BLX23T back
