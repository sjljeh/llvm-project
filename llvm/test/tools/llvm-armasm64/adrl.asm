; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefixes=CONTENTS,DISASM
; RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=RELOCS

        AREA |.text|, CODE, READONLY
        ADRL x0, target
labeled ADRL x1, target
target  ret
        END

; CONTENTS:      Contents of section .text:
; CONTENTS-NEXT: 0000 00000090 00000091 01000090 21000091
; CONTENTS-NEXT: 0010 c0035fd6

; DISASM:      0: 90000000      adrp x0, 0x0 <.text>
; DISASM-NEXT: 4: 91000000      add x0, x0, #0x0
; DISASM:      8: 90000001      adrp x1, 0x0 <.text>
; DISASM-NEXT: c: 91000021      add x1, x1, #0x0

; RELOCS:      0x0 IMAGE_REL_ARM64_PAGEBASE_REL21 target
; RELOCS-NEXT: 0x4 IMAGE_REL_ARM64_PAGEOFFSET_12A target
; RELOCS-NEXT: 0x8 IMAGE_REL_ARM64_PAGEBASE_REL21 target
; RELOCS-NEXT: 0xC IMAGE_REL_ARM64_PAGEOFFSET_12A target
