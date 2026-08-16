; RUN: llvm-armasm64 %s %t.obj > %t.output 2>&1
; RUN: FileCheck %s --check-prefix=WARN < %t.output
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS
; RUN: llvm-armasm64 -nowarn %s %t-nowarn.obj > %t.output 2>&1
; RUN: count 0 < %t.output
; RUN: llvm-armasm64 -ignore 4058,4045 %s %t-ignore.obj > %t.output 2>&1
; RUN: count 0 < %t.output
; RUN: llvm-armasm64 -ignore 9999 %s %t-unknown.obj 2>&1 | FileCheck %s --check-prefix=WARN
; RUN: llvm-armasm64 -errors %t.errors %s %t-errors.obj > %t.output 2>&1
; RUN: FileCheck %s --check-prefix=WARN < %t.errors
; RUN: count 0 < %t.output

        AREA |.data|, DATA, READWRITE
        GBLS TEXT
TEXT    SETS "variable"

        ASSERT {TRUE}, "ignored text"
        INFO 0, "plain warning"
        ! 0, TEXT :CC: " warning"

        MACRO
        REPORT $message
        INFO 0, "$message"
        MEND
        REPORT "macro warning"

        IF {FALSE}
        ASSERT {FALSE}
        INFO 1, "hidden error"
        ENDIF

        DCB 1
        END

; WARN-DAG: warning: {{.*}}diagnostics.asm:{{[0-9]+}}: A4058: plain warning
; WARN-DAG: warning: {{.*}}diagnostics.asm:{{[0-9]+}}: A4058: variable warning
; WARN-DAG: warning: {{.*}}diagnostics.asm:{{[0-9]+}}: A4058: macro warning
; CONTENTS: Contents of section .data:
; CONTENTS-NEXT: 0000 01
