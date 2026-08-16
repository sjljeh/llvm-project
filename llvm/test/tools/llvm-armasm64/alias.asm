; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols --relocations %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
original DCD 1
        ALIAS original, alternate
        EXPORT alternate[DATA]
        END

; CHECK:      Name: original
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .data
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Static
; CHECK:      Name: alternate
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .data
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: External
