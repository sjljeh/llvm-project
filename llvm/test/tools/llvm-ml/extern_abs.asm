; RUN: llvm-ml64 %s /Fo - | llvm-readobj --symbols - | FileCheck %s

EXTERN Target:ABS
ALIAS <Alias> = <Target>

; CHECK:      Name: Target
; CHECK:      StorageClass: External
; CHECK:      Name: Alias
; CHECK:      StorageClass: WeakExternal
; CHECK:      AuxWeakExternal {
; CHECK-NEXT:   Linked: Target
; CHECK-NEXT:   Search: Alias

END
