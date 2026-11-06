; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
        DCD 1
        END

; armasm writes @comp.id, @feat.00 and .file first, then the section symbols.
; CHECK:      Name: @comp.id
; CHECK-NEXT: Value: 17010639
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
; CHECK:      Name: .file
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: IMAGE_SYM_DEBUG
; CHECK:      StorageClass: File
; CHECK:      FileName: {{.*}}coff-metadata.asm
; CHECK:      Name: .data
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .data (1)
; CHECK:      AuxSectionDef {
; CHECK:      Checksum: 0x0
; CHECK-NEXT: Number: 0
; CHECK:      Name: .debug$S
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .debug$S (2)
