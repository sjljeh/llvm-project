; RUN: llvm-armasm64 %s %t.obj
; RUN: llvm-objdump -s -d %t.obj | FileCheck %s

        AREA |.text|, CODE, READONLY
        EXPORT extensions
extensions
        fadd h0, h0, h1
        sdot v0.4s, v1.16b, v2.16b
        ptrue p0.b
        fmlalb z0.s, z1.h, z2.h
        ret
        END

; CHECK: Contents of section .text:
; CHECK-NEXT: 0000 0028e11e 2094824e e0e31825 2080a264
; CHECK-NEXT: 0010 c0035fd6
; CHECK-LABEL: <extensions>:
; CHECK-NEXT: 0: 1ee12800      fadd h0, h0, h1
; CHECK-NEXT: 4: 4e829420      sdot v0.4s, v1.16b, v2.16b
; CHECK-NEXT: 8: 2518e3e0      ptrue p0.b
; CHECK-NEXT: c: 64a28020      fmlalb z0.s, z1.h, z2.h
; CHECK-NEXT: 10: d65f03c0      ret
