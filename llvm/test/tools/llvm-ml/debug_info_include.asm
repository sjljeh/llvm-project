; RUN: llvm-ml64 /Zi /I %S %s /Fo %t.obj
; RUN: llvm-readobj --codeview %t.obj | FileCheck %s

.code
INCLUDE Inputs/debug_info.inc
MainProc PROC
  nop
  ret
MainProc ENDP
END

; CHECK:      ArgList (0x1000)
; CHECK:      Procedure (0x1001)
; CHECK:      ArgList (0x1002)
; CHECK:      Procedure (0x1003)
; CHECK:      FileChecksum {
; CHECK:      Filename: {{.*}}debug_info_include.asm
; CHECK:      ChecksumKind: SHA256
; CHECK:      FileChecksum {
; CHECK:      Filename: {{.*}}debug_info.inc
; CHECK:      ChecksumKind: SHA256
; CHECK:      GlobalProcSym {
; CHECK:      FunctionType: void () (0x1001)
; CHECK:      DisplayName: IncludedProc
; CHECK:      GlobalProcSym {
; CHECK:      FunctionType: void () (0x1003)
; CHECK:      DisplayName: MainProc
; CHECK:      FunctionLineTable [
; CHECK:      Filename: {{.*}}debug_info.inc
; CHECK:      LineNumberStart: 1
; CHECK:      LineNumberStart: 2
; CHECK:      LineNumberStart: 3
; CHECK:      Filename: {{.*}}debug_info_include.asm
; CHECK:      LineNumberStart: 6
; CHECK:      LineNumberStart: 7
; CHECK:      LineNumberStart: 8
