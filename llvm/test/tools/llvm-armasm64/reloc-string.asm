; RUN: llvm-armasm64 %s %t.obj 2>&1 | FileCheck %s --check-prefix=WARNING
; RUN: llvm-armasm64 -ignore 4205 %s %t-ignore.obj 2>&1 | count 0
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS
; RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=RELOCS

        AREA |.data|, DATA, READWRITE
        IMPORT target
        DCB "AB"
        RELOC 2, target
        END

; WARNING: warning: {{.*}}reloc-string.asm:9: A4205: Previous data definition too small for requested relocation; emitting anyway
; CONTENTS:      Contents of section .data:
; CONTENTS-NEXT: 0000 4142
; RELOCS:        0x0 IMAGE_REL_ARM64_ADDR32NB target
