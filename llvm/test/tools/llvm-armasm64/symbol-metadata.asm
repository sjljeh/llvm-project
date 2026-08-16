; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
        ROUT
        b %F1
1       ret
named   ROUT
outside nop
proc    PROC
inside  nop
later   nop
        ENDP
        END

; CHECK:      Name: _lc001_000007_
; CHECK-NEXT: Value: 4
; CHECK-NEXT: Section: .text
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Label
; CHECK:      Name: named
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .text
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Static
; CHECK:      Name: proc
; CHECK-NEXT: Value: 12
; CHECK-NEXT: Section: .text
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Label
; CHECK:      Name: inside
; CHECK-NEXT: Value: 12
; CHECK-NEXT: Section: .text
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Function
; CHECK-NEXT: StorageClass: Label
; CHECK:      Name: later
; CHECK-NEXT: Value: 16
; CHECK-NEXT: Section: .text
; CHECK-NEXT: BaseType: Null
; CHECK-NEXT: ComplexType: Null
; CHECK-NEXT: StorageClass: Label
