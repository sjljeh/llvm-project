; RUN: llvm-ml64 %s /Fo - | llvm-readobj --symbols - | FileCheck %s

.code

PUBLIC alias_symbol

first PROC
target:
alias_symbol = target
  ret
first ENDP

; CHECK:      Name: alias_symbol
; CHECK-NEXT: Value: 0
; CHECK-NEXT: Section: .text
; CHECK:      StorageClass: External

END
