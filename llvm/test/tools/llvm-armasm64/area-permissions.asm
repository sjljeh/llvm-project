; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections %t.obj | FileCheck %s

        AREA code_write, CODE, READWRITE
        nop

        AREA bss_readonly, DATA, READONLY, NOINIT
        SPACE 4
        END

; CHECK:      Name: code_write
; CHECK:      Characteristics [
; CHECK:      IMAGE_SCN_CNT_CODE
; CHECK:      IMAGE_SCN_MEM_EXECUTE
; CHECK:      IMAGE_SCN_MEM_READ
; CHECK:      IMAGE_SCN_MEM_WRITE
; CHECK:      Name: bss_readonly
; CHECK:      Characteristics [
; CHECK:      IMAGE_SCN_CNT_UNINITIALIZED_DATA
; CHECK:      IMAGE_SCN_MEM_READ
; CHECK-NOT:  IMAGE_SCN_MEM_WRITE
