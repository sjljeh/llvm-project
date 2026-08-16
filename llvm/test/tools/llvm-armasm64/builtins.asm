; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefix=CONTENTS
; RUN: llvm-readobj --relocations --symbols %t.obj | FileCheck %s --check-prefix=METADATA

        AREA |.data|, DATA, READWRITE
        DCB {ARCHITECTURE},0
        DCB {CPU},0
        DCD {ARMASM_VERSION}

        IF {architecture} = "not specified"
        DCB 1
        ENDIF
        IF {cpu} = """-arch 4t"""
        DCB 2
        ENDIF

        GBLA VARIABLE
CONSTANT EQU 1
        IMPORT IMPORTED
        EXTERN EXTERNAL
        EXPORT EXPORTED
EXPORTED
LABEL
        MACRO
MACRO_NAME
        MEND

        IF :DEF:VARIABLE :LAND: :DEF:CONSTANT
        DCB 3
        ENDIF
        IF :DEF:IMPORTED :LAND: :DEF:EXTERNAL :LAND: :DEF:EXPORTED :LAND: :DEF:LABEL
        DCB 4
        ENDIF
        IF :LNOT::DEF:MACRO_NAME :LAND: :LNOT::DEF:UNKNOWN
        DCB 5
        ENDIF
        IF :DEF:|CONSTANT|
        DCB 6
        ENDIF
        IF :DEF:{architecture} :LAND: :DEF:{cpu} :LAND: :DEF:{armasm_version}
        DCB 7
        ENDIF
        IF :LNOT::DEF:ARCHITECTURE
        DCB 8
        ENDIF
        IF :DEF:LATER
        DCB 0
        ELSE
        DCB 9
        ENDIF
LATER

        AREA |.pcdata|, DATA, READWRITE
PC_START
        DCB 0xaa
        DCD {PC}
        DCB 0xbb,0xcc
        DCD {PC} - PC_START
        DCB 0xdd

        AREA |.text|, CODE, READONLY
CODE_START
        adr x0, {PC}
        ret
        END

; CONTENTS: Contents of section .data:
; CONTENTS-NEXT: 0000 6e6f7420 73706563 69666965 6400222d
; CONTENTS-NEXT: 0010 61726368 20347422 00000000 789aa608
; CONTENTS-NEXT: 0020 01020304 05060708 09
; CONTENTS: Contents of section .pcdata:
; CONTENTS-NEXT: 0000 aa000000 00000000 bbcc0000 0c000000
; CONTENTS-NEXT: 0010 dd
; CONTENTS: Contents of section .text:
; CONTENTS-NEXT: 0000 00000010 c0035fd6
; CONTENTS-LABEL: <|2>:
; CONTENTS-NEXT: 0: 10000000      adr x0, 0x0 <|2>
; CONTENTS-NEXT: 4: d65f03c0      ret

; METADATA: Section (2) .pcdata {
; METADATA-NEXT: 0x4 IMAGE_REL_ARM64_ADDR64 |0
; METADATA: Name: |0
; METADATA: Value: 4
; METADATA: Name: |1
; METADATA: Value: 12
; METADATA: Name: |2
; METADATA: Value: 0
