; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
kept    DCD 1
        KEEP kept
        END

; CHECK:      Name: kept
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .data
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Static
