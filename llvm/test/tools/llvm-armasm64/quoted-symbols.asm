; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --symbols %t.obj | FileCheck %s --check-prefix=SYMBOLS
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefixes=CONTENTS,DISASM

        EXPORT |symbol with space|[DATA]
        EXPORT |symbol-name|[DATA]
        AREA |.data|, DATA, READWRITE
|symbol with space| DCD 1
|symbol-name| DCD 2

        AREA |.text|, CODE, READONLY
        b |target-label|
|target-label| ret
        END

; SYMBOLS:      Name: symbol with space
; SYMBOLS-NEXT: Value: 0
; SYMBOLS-NEXT: Section: .data
; SYMBOLS:      StorageClass: External
; SYMBOLS:      Name: symbol-name
; SYMBOLS-NEXT: Value: 4
; SYMBOLS-NEXT: Section: .data
; SYMBOLS:      StorageClass: External
; SYMBOLS:      Name: target-label
; SYMBOLS-NEXT: Value: 4
; SYMBOLS-NEXT: Section: .text
; SYMBOLS:      StorageClass: Label

; CONTENTS:      Contents of section .data:
; CONTENTS-NEXT: 0000 01000000 02000000
; CONTENTS:      Contents of section .text:
; CONTENTS-NEXT: 0000 01000014 c0035fd6
; DISASM:        0: 14000001      b 0x4 <target-label>
