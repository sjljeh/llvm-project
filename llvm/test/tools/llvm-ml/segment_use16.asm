; RUN: llvm-ml -filetype=s %s /Fo - | FileCheck %s

option dotname

.text SEGMENT use16 PUBLIC 'CODE'
base:
mov word ptr cs:[target - base], ax
mov si, offset target - base
target:
.text ENDS

.code
mov eax, eax

; CHECK: .code16
; CHECK: .code32

END
