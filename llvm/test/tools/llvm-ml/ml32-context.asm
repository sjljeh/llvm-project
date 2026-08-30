; RUN: llvm-ml -filetype=s %s /Fo - | FileCheck %s

.386p
.model flat
assume fs:nothing, gs:nothing

.code
.fpo (0, 0, 0, 0, 0, 0)
mov eax, eax
end

; CHECK-NOT: 386p
; CHECK-NOT: model
; CHECK-NOT: flat
; CHECK-NOT: assume
; CHECK-NOT: fpo
