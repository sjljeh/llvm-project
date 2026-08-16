; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections --symbols %t.obj | FileCheck %s

        COMMON value, 4
        END

; CHECK:      Name: __DefaultSection
; CHECK:      RawDataSize: 0
; CHECK:      IMAGE_SCN_ALIGN_8BYTES
; CHECK:      IMAGE_SCN_CNT_INITIALIZED_DATA
; CHECK:      IMAGE_SCN_MEM_READ
; CHECK:      IMAGE_SCN_MEM_WRITE
; CHECK:      Name: value
; CHECK-NEXT: Value: 4
; CHECK-NEXT: Section: IMAGE_SYM_UNDEFINED
