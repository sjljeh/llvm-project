; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections --symbols --relocations --unwind %t.obj | FileCheck %s
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS

        ASSERT ({TRUE} || {FALSE})
        GBLA reg_number
reg_number SETA :RCONST:fp
        ASSERT reg_number = 29
        ASSERT (:RCONST:wsp = 31)
sdk_scope ROUT

        AREA |.text|, CODE, READONLY
        EXPORT probe
probe   PROC
prolog_instruction sub sp, sp, #32
        ret
probe_end
        ENDP

        AREA |.pdata|, PDATA
probe_pdata
        DCD probe
        RELOC 2
        DCD 0
        RELOC 2, probe_xdata

        AREA |.xdata|, DATA, READONLY
probe_xdata
        DCD (1 :SHL: 27) :OR: ((probe_end - probe) / 4)
        DCB 2, 0xe4, 0, 0

        AREA |.text|, CODE, READONLY
local_proc PROC {x0-x3}, {v0-v3}
        ret
local_proc ENDP
        END

; CHECK:      Name: .text
; CHECK:      RawDataSize: 12
; CHECK:      Name: .pdata
; CHECK:      RawDataSize: 8
; CHECK:      RelocationCount: 2
; CHECK:      IMAGE_SCN_MEM_READ
; CHECK-NOT:  IMAGE_SCN_MEM_WRITE
; CHECK:      Name: .xdata
; CHECK:      RawDataSize: 8
; CHECK:      Relocations [
; CHECK:        Section (3) .pdata {
; CHECK-NEXT:     0x0 IMAGE_REL_ARM64_ADDR32NB probe
; CHECK-NEXT:     0x4 IMAGE_REL_ARM64_ADDR32NB probe_xdata
; CHECK-NEXT:   }
; CHECK-NEXT: ]
; CHECK:      RuntimeFunction {
; CHECK-NEXT:   Function: probe
; CHECK-NEXT:   ExceptionRecord: probe_xdata
; CHECK:        FunctionLength: 8
; CHECK:        Prologue [
; CHECK-NEXT:     0x02                ; sub sp, #32
; CHECK-NEXT:     0xe4                ; end
; CHECK:      Name: local_proc
; CHECK:      StorageClass: Label

; CONTENTS:      Contents of section .text:
; CONTENTS-NEXT: 0000 ff8300d1 c0035fd6 c0035fd6
; CONTENTS:      Contents of section .pdata:
; CONTENTS-NEXT: 0000 00000000 00000000
; CONTENTS:      Contents of section .xdata:
; CONTENTS-NEXT: 0000 02000008 02e40000
