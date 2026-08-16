; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefixes=CONTENTS,DISASM
; RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=RELOCS

        AREA |.text|, CODE, READONLY
        IMPORT target
        nop
        RELOC 2, target
        BL target
        RELOC 3, target
        END

; CONTENTS:      Contents of section .text:
; CONTENTS-NEXT: 0000 1f2003d5 00000094
; DISASM:        0: d503201f      nop
; DISASM-NEXT:   4: 94000000      bl 0x4 <.text+0x4>

; RELOCS:      Section (1) .text {
; RELOCS-NEXT:   0x0 IMAGE_REL_ARM64_ADDR32NB target
; RELOCS-NEXT:   0x4 IMAGE_REL_ARM64_BRANCH26 target
; RELOCS-NEXT: }
