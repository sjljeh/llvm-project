; RUN: not llvm-armasm64 %s %t.obj 2>&1 | FileCheck %s

        AREA |.data|, DATA, READWRITE
        DCB 1,,2
        END

; CHECK: error: unknown token in expression
