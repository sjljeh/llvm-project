; RUN: llvm-ml64 /Cp -filetype=s %s /Fo - | FileCheck %s

OPTION DOTNAME

.PROC MACRO name
current_proc EQU %name
%name PROC FRAME
ENDM

.ENDP MACRO
%current_proc ENDP
ENDM

.code
.PROC TestProc
  .ENDPROLOG
  ret
.ENDP

; CHECK-LABEL: TestProc:
; CHECK: ret

END
