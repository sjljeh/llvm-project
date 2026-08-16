; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |default_align|, DATA, READWRITE, ALIGN=3
        DCB 1
        ALIGN
        DCB 2

        AREA |offset_align|, DATA, READWRITE, ALIGN=3
        DCB 1
        ALIGN 4, 3
        DCB 2

        AREA |padded_align|, DATA, READWRITE, ALIGN=3
        DCB 1, 1
        ALIGN 8, 0, 0xAABB, 2
        DCB 2

        AREA |zero_code_align|, CODE, READONLY, ALIGN=4
        nop
        ALIGN 16
        ret

        AREA |nop_code_align|, CODE, READONLY, CODEALIGN, ALIGN=4
        nop
        ALIGN 16
        ret
        END

; CHECK:      Contents of section default_align:
; CHECK-NEXT: 0000 01000000 02
; CHECK:      Contents of section offset_align:
; CHECK-NEXT: 0000 01000002
; CHECK:      Contents of section padded_align:
; CHECK-NEXT: 0000 0101bbaa bbaabbaa 02
; CHECK:      Contents of section zero_code_align:
; CHECK-NEXT: 0000 1f2003d5 00000000 00000000 00000000
; CHECK-NEXT: 0010 c0035fd6
; CHECK:      Contents of section nop_code_align:
; CHECK-NEXT: 0000 1f2003d5 1f2003d5 1f2003d5 1f2003d5
; CHECK-NEXT: 0010 c0035fd6
