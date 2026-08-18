; RUN: llvm-ml64 -filetype=s %s /Fo - | FileCheck %s

.const
scalar QWORD 1
vector QWORD 1, 2

.code
test_proc PROC
  vmovapd xmm0, vector
  andpd xmm0, scalar
  ret
test_proc ENDP

; CHECK-LABEL: test_proc:
; CHECK: vmovapd xmm0, xmmword ptr [rip + vector]
; CHECK: andpd xmm0, xmmword ptr [rip + scalar]

END
