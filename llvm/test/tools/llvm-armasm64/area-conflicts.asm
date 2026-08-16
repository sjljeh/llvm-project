; RUN: llvm-armasm64 %s %t.obj 2>&1 | FileCheck %s --check-prefix=WARNING
; RUN: llvm-armasm64 -ignore 4172 %s %t-ignore.obj 2>&1 | count 0
; RUN: llvm-readobj --sections %t.obj | FileCheck %s --check-prefix=SECTIONS

        AREA code_bss, CODE, READONLY, NOINIT
        SPACE 4

        AREA conflict, CODE, DATA, READONLY, READWRITE
        nop
        END

; WARNING-DAG: warning: {{.*}}area-conflicts.asm:5: A4172: illegal combination of section flags: section flags can not be inferred, code and data/uninitialized, readonly/readwrite
; WARNING-DAG: warning: {{.*}}area-conflicts.asm:8: A4172: illegal combination of section flags: section flags can not be inferred, code and data/uninitialized, readonly/readwrite

; SECTIONS:      Name: code_bss
; SECTIONS:      IMAGE_SCN_CNT_CODE
; SECTIONS:      IMAGE_SCN_MEM_EXECUTE
; SECTIONS:      IMAGE_SCN_MEM_READ
; SECTIONS-NOT:  IMAGE_SCN_MEM_WRITE
; SECTIONS:      Name: conflict
; SECTIONS:      IMAGE_SCN_CNT_CODE
; SECTIONS:      IMAGE_SCN_MEM_EXECUTE
; SECTIONS:      IMAGE_SCN_MEM_READ
; SECTIONS:      IMAGE_SCN_MEM_WRITE
