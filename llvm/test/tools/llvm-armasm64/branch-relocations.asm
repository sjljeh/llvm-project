; armasm64 leaves a BL to a symbol without an addend to the linker, even
; within a section, and resolves other branches it can; numeric local labels
; and {PC} are relocated against their own symbols. Only a label in column 1
; is a label, so "bl proc" branches to a symbol named proc. Checked against
; Microsoft armasm64.
; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -dr %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
        IMPORT ext
back    nop
        bl back
        b back
        bl fwd
        b fwd
        bl proc
        bl back+0
        bl back-4
        bl {PC}
        bl %f1
1       nop
        bl %b1
        bl ext
fwd     nop
proc    PROC
        ret
        ENDP
        AREA |.text2|, CODE, READONLY
        b back
        END

; CHECK-LABEL: <back>:
; CHECK-NEXT:   0: d503201f nop
; CHECK-NEXT:   4: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 back
; CHECK-NEXT:   8: 17fffffe b 0x0 <back>
; CHECK-NEXT:   c: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 fwd
; CHECK-NEXT:  10: 14000009 b 0x34 <fwd>
; CHECK-NEXT:  14: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 proc
; CHECK-NEXT:  18: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 back
; CHECK-NEXT:  1c: 97fffff8 bl 0xfffffffffffffffc
; CHECK-LABEL: <|0>:
; CHECK-NEXT:  20: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 |0
; CHECK-NEXT:  24: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 _lc001_000021_
; CHECK-LABEL: <_lc001_000021_>:
; CHECK-NEXT:  28: d503201f nop
; CHECK-NEXT:  2c: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 _lc001_000021_
; CHECK-NEXT:  30: 94000000 bl
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 ext
; CHECK-LABEL: <.text2>:
; CHECK-NEXT:   0: 14000000 b
; CHECK-NEXT:      IMAGE_REL_ARM64_BRANCH26 back
