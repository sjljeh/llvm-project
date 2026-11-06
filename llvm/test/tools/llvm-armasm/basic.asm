; RUN: llvm-armasm %s %t.obj
; RUN: llvm-objdump -dr --triple=thumbv7-windows %t.obj | FileCheck %s
; RUN: llvm-readobj --file-headers --sections --symbols %t.obj | FileCheck %s --check-prefix=OBJ
; RUN: llvm-readobj -r %t.obj | FileCheck %s --check-prefix=RELOCS
; The object path is recorded in .debug$S, so both runs write the same path.
; RUN: mv %t.obj %t.default.obj
; RUN: llvm-armasm64 -machine ARM %s %t.obj
; RUN: cmp %t.default.obj %t.obj

; Thumb-2 code as Microsoft armasm assembles it: MOV32, RN register aliases,
; BL with a BLX23T relocation and B.W with BRANCH24T, and DCD with ADDR32.
; The expected bytes come from armasm 14.44.

        AREA |.text|, CODE, READONLY
        IMPORT ext
        EXPORT f
cnt     RN r4
f       PROC
        mov32 r3, ext
        mov32 r2, 0x12345678
        adds cnt, cnt, #1
        bl ext
        b local
        nop
local
        b ext
        bx lr
        ENDP
        AREA |.data|, DATA
val     DCD f
        END

; CHECK:      0: f240 0300 movw r3, #0x0
; CHECK-NEXT: 00000000: IMAGE_REL_ARM_MOV32T ext
; CHECK-NEXT: 4: f2c0 0300 movt r3, #0x0
; CHECK-NEXT: 8: f245 6278 movw r2, #0x5678
; CHECK-NEXT: c: f2c1 2234 movt r2, #0x1234
; CHECK-NEXT: 10: 1c64 adds r4, r4, #0x1
; CHECK-NEXT: 12: f000 f800 bl
; CHECK-NEXT: 00000012: IMAGE_REL_ARM_BLX23T ext
; CHECK-NEXT: 16: e000 b
; CHECK-NEXT: 18: bf00 nop
; CHECK:      1a: f000 b800 b.w
; CHECK-NEXT: 0000001a: IMAGE_REL_ARM_BRANCH24T ext
; CHECK-NEXT: 1e: 4770 bx lr

; RELOCS:      Section (2) .data {
; RELOCS-NEXT:   0x0 IMAGE_REL_ARM_ADDR32 f

; OBJ:      Machine: IMAGE_FILE_MACHINE_ARMNT (0x1C4)
; OBJ:      Name: .text
; OBJ:      Characteristics [ (0x60320020)
; OBJ-NEXT:   IMAGE_SCN_ALIGN_4BYTES
; OBJ-NEXT:   IMAGE_SCN_CNT_CODE
; OBJ-NEXT:   IMAGE_SCN_MEM_16BIT
; OBJ-NEXT:   IMAGE_SCN_MEM_EXECUTE
; OBJ-NEXT:   IMAGE_SCN_MEM_PURGEABLE
; OBJ-NEXT:   IMAGE_SCN_MEM_READ
; OBJ:      Name: .data
; OBJ:      Characteristics [ (0xC0300040)
; OBJ:      Name: @comp.id
; OBJ-NEXT: Value: 17009053
; OBJ:      Name: @feat.00
; OBJ-NEXT: Value: 16
; OBJ:      Name: f
; OBJ-NEXT: Value: 0
; OBJ-NEXT: Section: .text
; OBJ-NEXT: BaseType: Null
; OBJ-NEXT: ComplexType: Null
; OBJ-NEXT: StorageClass: External
; OBJ:      Name: val
; OBJ-NEXT: Value: 0
; OBJ-NEXT: Section: .data
; OBJ-NEXT: BaseType: Null
; OBJ-NEXT: ComplexType: Null
; OBJ-NEXT: StorageClass: Static
; OBJ:      Name: local
; OBJ-NEXT: Value: 26
; OBJ-NEXT: Section: .text
; OBJ-NEXT: BaseType: Null
; OBJ-NEXT: ComplexType: Null
; OBJ-NEXT: StorageClass: Label
