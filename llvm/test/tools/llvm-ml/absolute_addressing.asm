; RUN: llvm-ml64 -filetype=obj %s /Fo %t.obj
; RUN: llvm-objdump -dr %t.obj | FileCheck %s

.code

t1:
mov al, byte ptr [06f6ch]
mov dword ptr [06f04h], eax

; CHECK-LABEL: <t1>:
; CHECK-NEXT: 0: 8a 04 25 6c 6f 00 00 movb 0x6f6c, %al
; CHECK-NEXT: 7: 89 04 25 04 6f 00 00 movl %eax, 0x6f04

END
