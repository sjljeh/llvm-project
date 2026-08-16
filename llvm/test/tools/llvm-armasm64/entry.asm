; RUN: llvm-armasm64 %s %t.obj 2>&1 | FileCheck %s --check-prefix=WARNING
; RUN: llvm-objdump -d %t.obj | FileCheck %s --check-prefix=CODE

        AREA |.text|, CODE, READONLY
        ENTRY
        ret
        END

; WARNING: warning: {{.*}}entry.asm:5: A4038: unimplemented directive entry
; CODE:      ret
