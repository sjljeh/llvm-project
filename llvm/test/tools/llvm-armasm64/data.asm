; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.obj | FileCheck %s --check-prefix=OBJ
; RUN: llvm-objdump -s %t.obj | FileCheck %s --check-prefix=CONTENTS

        AREA |.data|, DATA, READWRITE, ALIGN=3
        IMPORT external
data_start DCB 0x11
word_aligned DCW 0x2233
        DCB 0x44
word_unaligned DCWU 0x5566
        DCB 0x77
dword_aligned DCD 0x8899aabb
        DCB 0xcc
dword_unaligned DCDU 0xddeeff00
        DCB 0x12
qword_aligned DCQ 0x1122334455667788
        DCB 0x34
qword_unaligned DCQU 0x99aabbccddeeff00
bytes   = 1, 2, "A;,B" ; trailing comment
dwords & 0x66778899
aligned_ref DCD external
unaligned_ref DCDU external
qword_ref DCQ external
unaligned_qword_ref DCQU external
escaped DCB "a""b", "$$", "c\n"
        EXPORT data_start
        END

; OBJ: Name: .data
; OBJ: RawDataSize: 78
; OBJ: RelocationCount: 4
; OBJ: Offset: 0x30
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Offset: 0x34
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Offset: 0x38
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Offset: 0x40
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Name: data_start
; OBJ: Value: 0
; OBJ: Section: .data
; OBJ: StorageClass: External
; OBJ-DAG: Name: word_aligned
; OBJ-DAG: Name: dword_unaligned
; OBJ-DAG: Name: qword_unaligned

; CONTENTS: Contents of section .data:
; CONTENTS-NEXT: 0000 11003322 44665577 bbaa9988 cc00ffee
; CONTENTS-NEXT: 0010 dd120000 88776655 44332211 3400ffee
; CONTENTS-NEXT: 0020 ddccbbaa 99010241 3b2c4200 99887766
; CONTENTS-NEXT: 0030 00000000 00000000 00000000 00000000
; CONTENTS-NEXT: 0040 00000000 00000000 61226224 630a
