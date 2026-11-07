; RUN: llvm-armasm %s %t.obj
; RUN: llvm-readobj --sections --section-data %t.obj | FileCheck %s
; RUN: llvm-readobj -r %t.obj | FileCheck %s --check-prefix=RELOCS

; The operators and directives the Windows kernel's kxarm.h unwind macros
; rely on, as Microsoft armasm 14.44 assembles them:
; - :RCONST: numbers every 32-bit register class and alias, not only the
;   AArch64 names;
; - ?label of an instruction is its encoded Thumb-2 size, already within the
;   pass that defines the label (PROLOG_NOP picks 0xFB or 0xFC with it);
; - RELOC accepts a numeric IMAGE_REL_ARM_* type (2: ADDR32NB for .pdata).

myreg   RN      r7
myd     DN      d9

        AREA    |.data|, DATA
; CHECK:      Name: .data
; CHECK:      SectionData (
; CHECK-NEXT:   0000: 00040C0F 0D0E0F0C 0B090A03 081F0207  |................|
; CHECK-NEXT:   0010: 09000000                             |....|
        DCB     :RCONST:r0, :RCONST:r4, :RCONST:R12, :RCONST:r15
        DCB     :RCONST:sp, :RCONST:lr, :RCONST:pc, :RCONST:ip
        DCB     :RCONST:fp, :RCONST:sb, :RCONST:sl, :RCONST:s3
        DCB     :RCONST:d8, :RCONST:d31, :RCONST:q2, :RCONST:myreg
        DCB     :RCONST:myd, 0, 0, 0

        THUMB
        AREA    |.text|, CODE, READONLY
; CHECK:      Name: .text
; CHECK:      SectionData (
; CHECK-NEXT:   0000: 0DF1080B 084602AF 22220402 00000000  |.....F..""......|
wide    add     r11, sp, #8
narrow  mov     r0, r1
|Temp.1| add    r7, sp, #8
        IF ?|Temp.1| == 2
        DCW     0x2222
        ELSE
        DCW     0x4444
        ENDIF
        DCB     ?wide, ?narrow
        ALIGN   4
        DCD     wide
        RELOC   2

; RELOCS:      Section (2) .text {
; RELOCS-NEXT:   0xC IMAGE_REL_ARM_ADDR32NB wide
; RELOCS-NEXT: }

        END
