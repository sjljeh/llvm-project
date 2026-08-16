; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --relocations %t.obj | FileCheck %s
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS

        AREA |.pdata|, PDATA
target
        DCD 1, 2, 3, 4
        RELOC 2, target
        DCD 0, target
        RELOC 2
        END

; CHECK:      Relocations [
; CHECK-NEXT:   Section (1) .pdata {
; CHECK-NEXT:     0xC IMAGE_REL_ARM64_ADDR32NB target
; CHECK-NEXT:     0x14 IMAGE_REL_ARM64_ADDR32NB target
; CHECK-NEXT:   }
; CHECK-NEXT: ]

; CONTENTS:      Contents of section .pdata:
; CONTENTS-NEXT: 0000 01000000 02000000 03000000 04000000
; CONTENTS-NEXT: 0010 00000000 00000000
