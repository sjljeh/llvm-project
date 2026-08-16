; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections --section-symbols --relocations --unwind %t.obj | FileCheck %s

        AREA |.text|{|comdat_func|}, CODE, READONLY, ALIGN=4
        EXPORT comdat_func
comdat_func PROC
        sub sp, sp, #16
        ret
comdat_func_end
        ENDP

        AREA |.pdata|{|comdat_func_pdata|}, ALIGN=2, READONLY, ASSOC=|.text|{|comdat_func|}
comdat_func_pdata
        DCD comdat_func
        RELOC 2
        DCD comdat_func_xdata
        RELOC 2

        AREA |.xdata|{|comdat_func_xdata|}, ALIGN=2, READONLY, ASSOC=|.text|{|comdat_func|}
comdat_func_xdata
        DCD (1 :SHL: 27) :OR: ((comdat_func_end - comdat_func) / 4)
        DCB 1, 0xe4, 0, 0
        END

; CHECK:      Name: .text
; CHECK:      IMAGE_SCN_ALIGN_16BYTES
; CHECK:      IMAGE_SCN_LNK_COMDAT
; CHECK:      Selection: NoDuplicates
; CHECK:      Name: .pdata
; CHECK:      IMAGE_SCN_ALIGN_4BYTES
; CHECK:      IMAGE_SCN_LNK_COMDAT
; CHECK:      Selection: Associative
; CHECK:      AssocSection: .text
; CHECK:      Name: comdat_func_pdata
; CHECK:      StorageClass: Static
; CHECK:      Name: .xdata
; CHECK:      IMAGE_SCN_ALIGN_4BYTES
; CHECK:      IMAGE_SCN_LNK_COMDAT
; CHECK:      Selection: Associative
; CHECK:      AssocSection: .text
; CHECK:      Name: comdat_func_xdata
; CHECK:      StorageClass: Static
; CHECK:      0x0 IMAGE_REL_ARM64_ADDR32NB comdat_func
; CHECK-NEXT: 0x4 IMAGE_REL_ARM64_ADDR32NB comdat_func_xdata
; CHECK:      RuntimeFunction {
; CHECK-NEXT:   Function: comdat_func
; CHECK-NEXT:   ExceptionRecord: comdat_func_xdata
; CHECK:        FunctionLength: 8
; CHECK:        0x01                ; sub sp, #16
; CHECK-NEXT:   0xe4                ; end
