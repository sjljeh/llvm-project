; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-readobj --sections --symbols --relocations --expand-relocs %t.obj | FileCheck %s --check-prefix=OBJ
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s --check-prefixes=CONTENTS,DISASM

        AREA |.text|, CODE, READONLY, ALIGN=3
        EXPORT literals
        IMPORT external
literals PROC
        LDR w0, =0x1234
        LDR w1, =0x12345678
        LDR w8, =0x12345678
        LDR x2, =0x1234
        LDR x3, =0x1122334455667788
        LDR w4, =external
        LDR x5, =external+8
        B after_pool
        LTORG
after_pool
        LDR w6, =0x12345678
        LDR x7, =0x1122334455667788
        RET
literals ENDP

        AREA |immediates|, CODE, READONLY, ALIGN=3
immediate_values
        LDR w9, =-1
        LDR x10, =-1
        LDR w11, =&00ff00ff
        LDR x12, =&0000ffff0000ffff
        LDR w13, =2_101
        RET

        AREA |end_pool|, CODE, READONLY, ALIGN=3
end_pool_code
        LDR x14, =0x0123456789abcdef
        END

; OBJ: Name: .text
; OBJ: RawDataSize: 84
; OBJ: RelocationCount: 2
; OBJ: Offset: 0x20
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Offset: 0x34
; OBJ-NEXT: Type: IMAGE_REL_ARM64_ADDR64
; OBJ-NEXT: Symbol: external
; OBJ: Name: immediates
; OBJ: Name: literals
; OBJ: Value: 0
; OBJ: Section: .text
; OBJ: StorageClass: External
; OBJ: Name: after_pool
; OBJ: Value: 56

; CONTENTS: Contents of section .text:
; CONTENTS-NEXT: 0000 80468252 61010018 48010018 824682d2
; CONTENTS-NEXT: 0010 c3000058 04010018 45000058 07000014
; CONTENTS-NEXT: 0020 08000000 00000000 88776655 44332211
; CONTENTS-NEXT: 0030 78563412 00000000 c6000018 67000058
; CONTENTS-NEXT: 0040 c0035fd6 00000000 88776655 44332211
; CONTENTS-NEXT: 0050 78563412
; CONTENTS: Contents of section end_pool:
; CONTENTS-NEXT: 0000 4e000058 00000000 efcdab89 67452301
; DISASM-LABEL: <literals>:
; DISASM-NEXT: 0: 52824680      mov w0, #0x1234
; DISASM-NEXT: 4: 18000161      ldr w1, 0x30 <literals+0x30>
; DISASM-NEXT: 8: 18000148      ldr w8, 0x30 <literals+0x30>
; DISASM: 10: 580000c3      ldr x3, 0x28 <literals+0x28>
; DISASM: 18: 58000045      ldr x5, 0x20 <literals+0x20>
; DISASM-LABEL: <after_pool>:
; DISASM-NEXT: 38: 180000c6      ldr w6, 0x50 <after_pool+0x18>
; DISASM-NEXT: 3c: 58000067      ldr x7, 0x48 <after_pool+0x10>
; DISASM-LABEL: <immediates>:
; DISASM-NEXT: 0: {{[0-9a-f]+}}      mov w9, #-0x1
; DISASM-NEXT: 4: {{[0-9a-f]+}}      mov x10, #-0x1
; DISASM-NEXT: 8: {{[0-9a-f]+}}      mov w11, #0xff00ff
; DISASM-NEXT: c: {{[0-9a-f]+}}      mov x12, #0xffff0000ffff
; DISASM-NEXT: 10: {{[0-9a-f]+}}      mov w13, #0x5
; DISASM-LABEL: <end_pool_code>:
; DISASM-NEXT: 0: 5800004e      ldr x14, 0x8 <end_pool_code+0x8>
