; RUN: llvm-ml -filetype=s %s /Fo - | FileCheck %s
; RUN: llvm-ml64 -filetype=s %s /Fo - | FileCheck %s

.code

first PROC
local_label:
  mov eax, 1
  jmp local_label
  call second
first ENDP

second PROC
local_label:
  mov eax, 2
  jmp local_label
second ENDP

global_label::
  ret

; CHECK-LABEL: first:
; CHECK: local_label:
; CHECK: mov eax, 1
; CHECK: jmp local_label
; CHECK: call second
; CHECK-LABEL: second:
; CHECK: local_label:
; CHECK: mov eax, 2
; CHECK: jmp local_label
; CHECK-LABEL: global_label:
; CHECK: ret

END
