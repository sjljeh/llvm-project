; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections --symbols --relocations %t.obj | FileCheck %s --check-prefix=OBJ --implicit-check-not="Name: MASK" --implicit-check-not="Name: unused_external"
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefix=DISASM

MASK    EQU (1 << 1)
        EXPORT test_proc
        EXTERN helper
        EXTERN unused_external
        AREA |.text|, CODE, READONLY, ALIGN=4
test_proc PROC
        msr daifset, #MASK
        bl helper
        b done
unused
        nop
done
        DCD 0x12345678
test_proc ENDP
        END

; OBJ: Name: .text
; OBJ: IMAGE_SCN_ALIGN_16BYTES
; OBJ: IMAGE_SCN_CNT_CODE
; OBJ: IMAGE_REL_ARM64_BRANCH26 helper
; OBJ: Name: test_proc
; OBJ: Section: .text
; OBJ: StorageClass: External
; OBJ: Name: helper
; OBJ: Section: IMAGE_SYM_UNDEFINED
; OBJ: Name: done
; OBJ: StorageClass: Label
; OBJ: Name: unused
; OBJ: StorageClass: Label

; DISASM: Contents of section .text:
; DISASM-NEXT: 0000 df4203d5 00000094 02000014 1f2003d5
; DISASM-NEXT: 0010 78563412
; DISASM-LABEL: <test_proc>:
; DISASM-NEXT: 0: d50342df      msr DAIFSet, #0x2
; DISASM-NEXT: 4: 94000000      bl 0x4 <test_proc+0x4>
; DISASM-NEXT: 8: 14000002      b 0x10 <done>
