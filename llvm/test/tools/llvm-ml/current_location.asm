; RUN: llvm-ml64 %s /Fo - | llvm-readobj --relocations - | FileCheck %s

.code

jmp $ + 2
ret

; CHECK: Relocations [
; CHECK-NEXT: ]

END
