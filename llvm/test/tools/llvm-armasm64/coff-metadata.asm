; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
        DCD 1
        END

; CHECK:      Name: .data
; CHECK:      AuxSectionDef {
; CHECK:      Checksum: 0x0
; CHECK-NEXT: Number: 0
; CHECK:      Name: @comp.id
; CHECK-NEXT: Value: 17010072
; CHECK-NEXT: Section: IMAGE_SYM_ABSOLUTE
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Static
; CHECK:      Name: @feat.00
; CHECK-NEXT: Value: 16
; CHECK-NEXT: Section: IMAGE_SYM_ABSOLUTE
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Static
