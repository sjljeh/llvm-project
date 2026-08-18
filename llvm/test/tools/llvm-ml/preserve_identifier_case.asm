; RUN: llvm-ml64 /Cp %s /Fo %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=DATA
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s --check-prefix=SYMBOL

OPTION DOTNAME

.data

.ASCII MACRO
  BYTE 1
ENDM

.ascii MACRO
  BYTE 2
ENDM

CREATE_ALIAS MACRO alias, target
  EXTERN &target:PROC
  ALIAS <&alias> = <&target>
ENDM

.ASCII
.ascii
CREATE_ALIAS AliasName, ActualName

; DATA: Contents of section .data:
; DATA-NEXT: 0000 0102

; SYMBOL:      Name: ActualName
; SYMBOL:      Name: AliasName
; SYMBOL:      StorageClass: WeakExternal
; SYMBOL:      AuxWeakExternal {
; SYMBOL-NEXT:   Linked: ActualName
; SYMBOL-NEXT:   Search: Alias

END
