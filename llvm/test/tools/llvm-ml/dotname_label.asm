; RUN: llvm-ml64 -filetype=s %s /Fo - | FileCheck %s

OPTION DOTNAME

.code

test_proc PROC
  jmp .local_label
.local_label:
  ret
test_proc ENDP

; CHECK-LABEL: test_proc:
; CHECK: jmp .local_label
; CHECK-LABEL: .local_label:
; CHECK: ret

END
