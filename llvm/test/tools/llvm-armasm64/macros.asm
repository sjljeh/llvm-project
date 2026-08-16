; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s %t.obj | FileCheck %s

        AREA |.data|, DATA, READWRITE

        MACRO
$label  EMIT $first,$second
$label  DCB $first
        DCB $second
        MEND

first   EMIT 1,2
        EMIT 3,4

        MACRO
        LOCAL_EMIT $start
        LCLA value
value   SETA $start
        WHILE value < $start + 2
        DCB value
value   SETA value + 1
        WEND
        MEND

        LOCAL_EMIT 5
        LOCAL_EMIT 7

        MACRO
        EXIT_IF $value
        DCB $value
        IF $value = 9
        MEXIT
        ENDIF
        DCB 0
        MEND

        EXIT_IF 9
        EXIT_IF 10

        MACRO
        INNER $value
        DCB $value
        MEND

        MACRO
        OUTER $value
        INNER $value
        INNER $value + 1
        MEND

        OUTER 11

        GBLA SHADOW
SHADOW  SETA 32
        MACRO
        USE_SHADOW
        LCLA SHADOW
SHADOW  SETA 13
        DCB SHADOW
        MEND
        USE_SHADOW
        USE_SHADOW
        DCB SHADOW

        MACRO
        INNER_LOCAL
        LCLA LOCAL_VALUE
LOCAL_VALUE SETA 15
        DCB LOCAL_VALUE
        MEND

        MACRO
        OUTER_LOCAL
        LCLA LOCAL_VALUE
LOCAL_VALUE SETA 14
        DCB LOCAL_VALUE
        INNER_LOCAL
        DCB LOCAL_VALUE
        MEND
        OUTER_LOCAL

        MACRO
        TEXT $value
        DCB "$value"
        MEND
        TEXT "AB"

        MACRO
        OPTIONAL $value
        IF "$value" = ""
        DCB 16
        ELSE
        DCB $value
        ENDIF
        MEND
        OPTIONAL
        OPTIONAL 17

        MACRO
        DIRECTIVE $prefix
        $prefix.B 18
        MEND
        DIRECTIVE DC

        MACRO
        LOCAL_TYPES
        LCLL FLAG
        LCLS TEXT_VALUE
        LCLA RESET_VALUE
FLAG    SETL {TRUE}
TEXT_VALUE SETS "CD"
RESET_VALUE SETA 20
        LCLA RESET_VALUE
        IF FLAG
        DCB TEXT_VALUE
        ENDIF
        DCB RESET_VALUE
        MEND
        LOCAL_TYPES

        GBLA GLOBAL_COUNT
GLOBAL_COUNT SETA 1
        MACRO
        BUMP_GLOBAL
GLOBAL_COUNT SETA $GLOBAL_COUNT + 1
        MEND
        BUMP_GLOBAL
        DCB GLOBAL_COUNT

        IF {FALSE}
        UNKNOWN_MACRO
        MEXIT
        ENDIF

        INCLUDE Inputs/macro.inc
included_label INCLUDED 19
        END

; CHECK: Contents of section .data:
; CHECK-NEXT: 0000 01020304 05060708 090a000b 0c0d0d20
; CHECK-NEXT: 0010 0e0f0e41 42101112 43440002 13
