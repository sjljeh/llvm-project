; RUN: llvm-ml64 -filetype=s %s /Fo - | FileCheck %s

Status RECORD FBusy: 1,
              FCode: 2,
              FError: 5

.code

mov eax, FBusy
mov eax, FCode
mov eax, FError
mov eax, MASK FBusy
mov eax, MASK FCode
mov eax, MASK FError

; CHECK: mov eax, 7
; CHECK: mov eax, 5
; CHECK: mov eax, 0
; CHECK: mov eax, 128
; CHECK: mov eax, 96
; CHECK: mov eax, 31

END
