; RUN: llvm-ml64 /Cp -filetype=s %s /Fo - | FileCheck %s

OPTION DOTNAME

.PROC MACRO name
current_proc EQU %name
%name PROC FRAME
ENDM

.ENDP MACRO
%current_proc ENDP
ENDM

STUBCODE MACRO Name, SyscallId, ArgCount
  .ENDPROLOG
  mov eax, SyscallId
  ret
ENDM

START_PROC MACRO Name, StackBytes
  .PROC &Name
ENDM

STUB MACRO Name, ArgCount
  START_PROC Nt&Name, %ArgCount * 4
  STUBCODE Name, SyscallId, %ArgCount
  .ENDP
  SyscallId = SyscallId + 1
ENDM

.code
SyscallId = 1000h
STUB Test, 1
STUB Test01, 1
STUB Test02, 1
STUB Test03, 1
STUB Test04, 1
STUB Test05, 1
STUB Test06, 1
STUB Test07, 1
STUB Test08, 1
STUB Test09, 1
STUB Test10, 1
STUB Test11, 1
STUB Test12, 1
STUB Test13, 1
STUB Test14, 1
STUB Test15, 1
STUB Test16, 1
STUB Test17, 1
STUB Test18, 1
STUB Test19, 1
STUB Test20, 1

; CHECK-LABEL: NtTest:
; CHECK: mov eax, 4096
; CHECK: ret
; CHECK-LABEL: NtTest20:
; CHECK: mov eax, 4116
; CHECK: ret

END
