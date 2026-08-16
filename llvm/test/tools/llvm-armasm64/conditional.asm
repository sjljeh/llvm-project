; RUN: llvm-armasm64 -predefine "FEATURE SETL {TRUE}" %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE
        GBLA COUNT
COUNT   SETA 0

        IF {FALSE}
        DCB 0
        ELIF COUNT = 0
        DCB 1
        ELSE
        DCB 2
        ENDIF

        IF {TRUE}
          IF {FALSE}
          DCB 3
          ELSE
          DCB 4
          ENDIF
        ENDIF

        WHILE COUNT < 4
        DCB COUNT
COUNT   SETA COUNT + 1
        WEND

        WHILE {FALSE}
        not_an_instruction
        WEND

        IF {FALSE}
        INCLUDE does-not-exist.inc
        DCB unknown_variable
          IF 1 / 0 = 0
          DCB 5
          ENDIF
        END
        ENDIF

        IF {TRUE}
        DCB 5
        ELIF {TRUE}
        DCB 6
        ELSE
        not_an_instruction
        ENDIF

        [ 0
        not_an_instruction
        |
        DCB 7
        ]

        GBLA INCLUDE_COUNT
INCLUDE_COUNT SETA 0
        WHILE INCLUDE_COUNT < 2
        INCLUDE Inputs/conditional.inc
INCLUDE_COUNT SETA INCLUDE_COUNT + 1
        WEND

        IF :DEF:FEATURE
          IF FEATURE
          DCB 9
          ENDIF
        ENDIF

        GBLA OUTER
        GBLA INNER
OUTER   SETA 0
        WHILE OUTER < 2
INNER   SETA 0
          WHILE INNER < 2
            IF INNER = OUTER
            DCB 10
            ELSE
            DCB 11
            ENDIF
INNER   SETA INNER + 1
          WEND
OUTER   SETA OUTER + 1
        WEND

CONDITION EQU 1
        IF CONDITION
        DCB 12
        ENDIF
        END

; CHECK: Contents of section .data:
; CHECK-NEXT: 0000 01040001 02030507 0808090a 0b0b0a0c
