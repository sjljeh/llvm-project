; RUN: llvm-ml -filetype=s %s /Fo - | FileCheck %s

option dotname

.text SEGMENT use16 PUBLIC 'CODE'
base:
mov word ptr cs:[target - base], ax
mov si, offset target - base
target:
.text ENDS

mov eax, eax

.code

; CHECK: .code16
; CHECK: .code32
; CHECK: movl %eax, %eax

END
