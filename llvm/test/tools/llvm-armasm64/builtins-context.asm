; RUN: llvm-armasm64 %s %t.obj 2>&1 | FileCheck %s --check-prefix=INFO
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS

        AREA |probe_area|, DATA, READWRITE
        INFO 0, {AREANAME}
        INFO 0, :STR:{CODESIZE}
        INFO 0, :STR:{CONFIG}
        INFO 0, {COMMANDLINE}
        INFO 0, {ENDIAN}
        INFO 0, {FPU}
        INFO 0, {INPUTFILE}
        INFO 0, :STR:{LINENUM}
        INFO 0, :STR:{OPT}
        INFO 0, :STR:{VAR}
        MAP 5
slot    FIELD 3
        INFO 0, :STR:{VAR}

        IF :DEF:{AREANAME} :LAND: :DEF:{CODESIZE} :LAND: :DEF:{CONFIG} :LAND: :DEF:{COMMANDLINE}
        IF :DEF:{ENDIAN} :LAND: :DEF:{FPU} :LAND: :DEF:{INPUTFILE} :LAND: :DEF:{LINENUM}
        IF :DEF:{OPT} :LAND: :DEF:{VAR}
        DCB 1
        ENDIF
        ENDIF
        ENDIF
        END

; INFO: {{.*}}builtins-context.asm(5) : warning A4058: probe_area
; INFO: {{.*}}builtins-context.asm(6) : warning A4058: 00000020
; INFO: {{.*}}builtins-context.asm(7) : warning A4058: 00000020
; INFO: {{.*}}builtins-context.asm(8) : warning A4058: {{.*}}llvm-armasm64{{.*}}builtins-context.asm
; INFO: {{.*}}builtins-context.asm(9) : warning A4058: little
; INFO: {{.*}}builtins-context.asm(10) : warning A4058: vfpv3
; INFO: {{.*}}builtins-context.asm(11) : warning A4058: {{.*}}builtins-context.asm
; INFO: {{.*}}builtins-context.asm(12) : warning A4058: 0000000C
; INFO: {{.*}}builtins-context.asm(13) : warning A4058: 00000001
; INFO: {{.*}}builtins-context.asm(14) : warning A4058: 00000000
; INFO: {{.*}}builtins-context.asm(17) : warning A4058: 00000008

; CONTENTS:      Contents of section probe_area:
; CONTENTS-NEXT: 0000 01
