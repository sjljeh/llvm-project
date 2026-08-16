; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s

        GBLS name
name    SETS "first"
        EXPORT $name[DATA]

        AREA |.data|, DATA, READWRITE
first   DCD 1
second  DCD 2

name    SETS "second"
        EXPORT $name[DATA]
        END

; CHECK:      Name: first
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .data
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: External
; CHECK:      Name: second
; CHECK-NEXT: Value: 4
; CHECK-NEXT: Section: .data
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: External
