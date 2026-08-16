; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.obj | FileCheck %s --check-prefix=OBJ
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefixes=CONTENTS,DISASM

        AREA |.data|, DATA, READWRITE, ALIGN=3
        IMPORT external
int_data
        DCB -128, 255, 'A', "B", 2_101, &7f
        DCW -32768, 65535, 2_11001010, 'Z'
        DCWU 0x1234
        DCD 1 :OR: 2, 4 :SHL: 4, external, external + (2 :SHL: 2)
        DCDU &11223344
        DCQ -1, 0x1122334455667788
        DCQU 2_101
float_data
        DCFS 1.0, -0.0, 3.40282347e+38, 0x7fc00001
        DCFSU .5
        DCFD 1.0, -4E-100, &fff0000000000000
        DCFDU .5
strings DCB "A" :CC: "B", :CHR:67, "DEF" :LEFT: 2

        AREA |.text|, CODE, READONLY, ALIGN=2
encoded
        DCI.W 0xd503201f
        DCI 0xd65f03c0
        END

; OBJ: Name: .data
; OBJ: RawDataSize: 117
; OBJ: RelocationCount: 2
; OBJ: Name: .text
; OBJ: RawDataSize: 8
; OBJ: Offset: 0x18
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Offset: 0x1C
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Name: int_data
; OBJ: Value: 0
; OBJ: Name: float_data
; OBJ: Value: 60
; OBJ: Name: strings
; OBJ: Value: 112
; OBJ: Name: encoded
; OBJ: Value: 0

; CONTENTS: Contents of section .data:
; CONTENTS-NEXT: 0000 80ff4142 057f0080 ffffca00 5a003412
; CONTENTS-NEXT: 0010 03000000 40000000 00000000 08000000
; CONTENTS-NEXT: 0020 44332211 ffffffff ffffffff 88776655
; CONTENTS-NEXT: 0030 44332211 05000000 00000000 0000803f
; CONTENTS-NEXT: 0040 00000080 ffff7f7f 0080ff4e 0000003f
; CONTENTS-NEXT: 0050 00000000 0000f03f 30058ee4 2eff4bab
; CONTENTS-NEXT: 0060 00000000 000030c3 00000000 0000e03f
; CONTENTS-NEXT: 0070 41424344 45
; CONTENTS: Contents of section .text:
; CONTENTS-NEXT: 0000 1f2003d5 c0035fd6
; DISASM-LABEL: <encoded>:
; DISASM-NEXT: 0: d503201f      nop
; DISASM-NEXT: 4: d65f03c0      ret
