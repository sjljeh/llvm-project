; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefix=CONTENTS
; RUN: llvm-readobj --sections --symbols %t.obj | FileCheck %s --check-prefix=OBJ --implicit-check-not="Name: Abs" --implicit-check-not="Name: Reg"

        AREA |.data|, DATA, READWRITE
Start   SPACE 3
Zero    FILL 4
Half    FILL 8, 0x1234, 2
Word    FILL 8, 0x12345678, 4
Percent % 2

        AREA |.bss|, DATA, READWRITE, NOINIT
Bss     SPACE 5
BssFill FILL 4, 0, 2

        AREA |.text|, CODE, READONLY
        mov x7, Abs
        MAP 16
Abs     FIELD 4
        FIELD 2
Abs2    FIELD 8
        mov x0, Abs
        add x1, x2, Abs
        ldr x3, [x4, Abs2]
        ^ 8, x9
Reg     # 4
        ldr x5, Reg
        str x6, Reg
        END

; CONTENTS:      Contents of section .data:
; CONTENTS-NEXT: 0000 00000000 00000034 12341234 12341278
; CONTENTS-NEXT: 0010 56341278 56341200 00
; CONTENTS:      Contents of section .bss:
; CONTENTS-NEXT: <skipping contents of bss section at [0000, 0009)>
; CONTENTS-LABEL: Disassembly of section .text:
; CONTENTS:      0: d2800207      mov x7, #0x10
; CONTENTS-NEXT: 4: d2800200      mov x0, #0x10
; CONTENTS-NEXT: 8: 91004041      add x1, x2, #0x10
; CONTENTS-NEXT: c: f8416083      ldur x3, [x4, #0x16]
; CONTENTS-NEXT: 10: f9400525     ldr x5, [x9, #0x8]
; CONTENTS-NEXT: 14: f9000526     str x6, [x9, #0x8]

; OBJ:      Name: .data
; OBJ:      RawDataSize: 25
; OBJ:      Name: .bss
; OBJ:      RawDataSize: 9
; OBJ-DAG:  Name: Start
; OBJ-DAG:  Value: 0
; OBJ-DAG:  Name: Zero
; OBJ-DAG:  Value: 3
; OBJ-DAG:  Name: Half
; OBJ-DAG:  Value: 7
; OBJ-DAG:  Name: Word
; OBJ-DAG:  Value: 15
; OBJ-DAG:  Name: Percent
; OBJ-DAG:  Value: 23
; OBJ-DAG:  Name: Bss
; OBJ-DAG:  Value: 0
; OBJ-DAG:  Name: BssFill
; OBJ-DAG:  Value: 5
