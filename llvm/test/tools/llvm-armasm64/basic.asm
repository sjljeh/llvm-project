; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --file-headers --sections --symbols %t.obj | FileCheck %s --check-prefix=OBJ
; RUN: llvm-objdump -d %t.obj | FileCheck %s --check-prefix=DISASM

        EXPORT test_wfi
        AREA TestCode, CODE, READONLY
test_wfi
        wfi
        ret
        END

; OBJ: Format: COFF-ARM64
; OBJ: Arch: aarch64
; OBJ: Name: TestCode
; OBJ: IMAGE_SCN_ALIGN_8BYTES
; OBJ: IMAGE_SCN_CNT_CODE
; OBJ: IMAGE_SCN_MEM_EXECUTE
; OBJ: IMAGE_SCN_MEM_READ
; OBJ: Name: test_wfi
; OBJ: Section: TestCode
; OBJ: StorageClass: External

; DISASM-LABEL: <test_wfi>:
; DISASM-NEXT: 0: d503207f      wfi
; DISASM-NEXT: 4: d65f03c0      ret
