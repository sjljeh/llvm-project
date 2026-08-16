; RUN: llvm-armasm64 %s %t.obj 2>&1 | FileCheck %s --check-prefix=WARNING
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS
; RUN: llvm-readobj --sections %t.obj | FileCheck %s --check-prefix=SECTIONS

        AREA same, DATA, READWRITE, ALIGN=2
        DCB 1
        AREA same, DATA, READWRITE, ALIGN=2
        DCB 2

        AREA changed, DATA, READWRITE, ALIGN=2
        DCB 3
        AREA changed, CODE, READONLY, ALIGN=4
        DCB 4
        END

; WARNING: warning: {{.*}}area-redefinition.asm:12: A4043: redefinition of section flags ignored
; WARNING-NOT: A4043

; CONTENTS:      Contents of section same:
; CONTENTS-NEXT: 0000 0102
; CONTENTS:      Contents of section changed:
; CONTENTS-NEXT: 0000 0304

; SECTIONS:      Name: same
; SECTIONS:      RawDataSize: 2
; SECTIONS:      IMAGE_SCN_ALIGN_4BYTES
; SECTIONS:      IMAGE_SCN_CNT_INITIALIZED_DATA
; SECTIONS:      IMAGE_SCN_MEM_WRITE
; SECTIONS:      Name: changed
; SECTIONS:      RawDataSize: 2
; SECTIONS:      IMAGE_SCN_ALIGN_4BYTES
; SECTIONS:      IMAGE_SCN_CNT_INITIALIZED_DATA
; SECTIONS:      IMAGE_SCN_MEM_WRITE
