; RUN: llvm-armasm %s %t.obj 2>&1 | FileCheck %s --check-prefix=WARN
; RUN: llvm-objdump -d --triple=thumbv7-windows %t.obj | FileCheck %s
; RUN: llvm-armasm -oldit %s %t.oldit.obj 2>&1 | count 0
; RUN: llvm-objdump -d --triple=thumbv7-windows %t.oldit.obj | FileCheck %s --check-prefix=OLDIT

; armasm generates IT instructions for conditional Thumb instructions. By
; default each conditional instruction gets its own IT instruction, as ARMv8
; recommends, and 32-bit instructions in IT blocks draw warning A4509.
; -oldit merges consecutive instructions into pre-ARMv8 IT blocks.

        AREA |.text|, CODE, READONLY
f       PROC
        cmp r0, #0
        moveq r0, #1
        moveq r1, #2
        movne r2, #3
        addeq r0, r0, r1
        ldreq r0, [r1]
        addeq.w r0, r0, #0x1000
        bx lr
        ENDP
        END

; WARN: it-blocks.asm(19) : warning A4509: This form of conditional instruction is deprecated
; WARN-NEXT: {{^}}        addeq.w r0, r0, #0x1000{{$}}

; CHECK:      0: 2800 cmp r0, #0x0
; CHECK-NEXT: 2: bf08 it eq
; CHECK-NEXT: 4: 2001 moveq r0, #0x1
; CHECK-NEXT: 6: bf08 it eq
; CHECK-NEXT: 8: 2102 moveq r1, #0x2
; CHECK-NEXT: a: bf18 it ne
; CHECK-NEXT: c: 2203 movne r2, #0x3
; CHECK-NEXT: e: bf08 it eq
; CHECK-NEXT: 10: 1840 addeq r0, r0, r1
; CHECK-NEXT: 12: bf08 it eq
; CHECK-NEXT: 14: 6808 ldreq r0, [r1]
; CHECK-NEXT: 16: bf08 it eq
; CHECK-NEXT: 18: f500 5080 addeq.w r0, r0, #0x1000
; CHECK-NEXT: 1c: 4770 bx lr

; OLDIT:      0: 2800 cmp r0, #0x0
; OLDIT-NEXT: 2: bf05 ittet eq
; OLDIT-NEXT: 4: 2001 moveq r0, #0x1
; OLDIT-NEXT: 6: 2102 moveq r1, #0x2
; OLDIT-NEXT: 8: 2203 movne r2, #0x3
; OLDIT-NEXT: a: 1840 addeq r0, r0, r1
; OLDIT-NEXT: c: bf04 itt eq
; OLDIT-NEXT: e: 6808 ldreq r0, [r1]
; OLDIT-NEXT: 10: f500 5080 addeq.w r0, r0, #0x1000
; OLDIT-NEXT: 14: 4770 bx lr
