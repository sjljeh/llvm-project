; RUN: llvm-ml -filetype=s %s /Fo - | FileCheck %s
; RUN: llvm-ml -filetype=obj %s /Fo - | llvm-readobj --relocations - | FileCheck %s --check-prefix=RELOC

.386p
.model flat
assume fs:nothing, gs:nothing
TEB_EXCEPTION_LIST equ 0

.code
.fpo (0, 0, 0, 0, 0, 0)
mov eax, eax
target:
push offset target
push [fs:TEB_EXCEPTION_LIST]
mov [fs:TEB_EXCEPTION_LIST], esp
mov esp, [fs:TEB_EXCEPTION_LIST]
pop [fs:TEB_EXCEPTION_LIST]
end

; CHECK-NOT: 386p
; CHECK-NOT: model
; CHECK-NOT: flat
; CHECK-NOT: assume
; CHECK-NOT: fpo
; CHECK: push dword ptr fs:[0]
; CHECK: mov dword ptr fs:[0], esp
; CHECK: mov esp, dword ptr fs:[0]
; CHECK: pop dword ptr fs:[0]

; RELOC: IMAGE_REL_I386_DIR32
