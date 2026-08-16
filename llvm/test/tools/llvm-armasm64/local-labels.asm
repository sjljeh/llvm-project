; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS

        AREA |.text|, CODE, READONLY
        ROUT
        b %F1
        nop
1       ret
        b %B1

named   ROUT
        b %F1named
        nop
1named  ret
        b %B1named

        MACRO
        LOCAL_BRANCH
        b %F2
        nop
2       nop
        MEND
        LOCAL_BRANCH
        LOCAL_BRANCH
        END

; CONTENTS:      Contents of section .text:
; CONTENTS-NEXT: 0000 02000014 1f2003d5 c0035fd6 ffffff17
; CONTENTS-NEXT: 0010 02000014 1f2003d5 c0035fd6 ffffff17
; CONTENTS-NEXT: 0020 02000014 1f2003d5 1f2003d5 02000014
; CONTENTS-NEXT: 0030 1f2003d5 1f2003d5

; SYMBOLS-DAG: Name: _lc001_000008_
; SYMBOLS-DAG: Name: named
; SYMBOLS-DAG: Name: _lc001_000014_
; SYMBOLS-DAG: Name: _lc002_000026_
; SYMBOLS-DAG: Name: _lc002_000030_
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s --check-prefix=SYMBOLS
