; RUN: llvm-ml -filetype=s %s /Fo - | FileCheck %s
; RUN: llvm-ml -filetype=obj %s /Fo - | llvm-readobj --relocations - | FileCheck %s --check-prefix=RELOC

.386p
.model flat
assume fs:nothing, gs:nothing

.code
.fpo (0, 0, 0, 0, 0, 0)
mov eax, eax
target:
push offset target
end

; CHECK-NOT: 386p
; CHECK-NOT: model
; CHECK-NOT: flat
; CHECK-NOT: assume
; CHECK-NOT: fpo

; RELOC: IMAGE_REL_I386_DIR32
