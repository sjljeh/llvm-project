// REQUIRES: arm
// RUN: llvm-mc -filetype=obj -triple=thumbv7-windows %s -o %t.obj
// RUN: lld-link -lldmingw %t.obj %p/Inputs/library.lib /subsystem:console \
// RUN:   /entry:main /delayload:library.dll /out:%t.exe
// RUN: llvm-objdump --no-print-imm-hex -d %t.exe | FileCheck %s

// __delayLoadHelper2 is more than 16 MiB before the tail merge, out of reach of
// a bl. The tail merge loads its address, with the Thumb bit set, and uses blx.
// link.exe-compatible layouts call the helper with a bl, as link.exe does, so
// this links in MinGW mode.

// CHECK:      push.w {r0, r1, r2, r3, r11, lr}
// CHECK:      movw r2, #4109
// CHECK-NEXT: movt r2, #64
// CHECK-NEXT: blx r2

    .syntax unified
    .text
    .globl main
    .def main; .scl 2; .type 32; .endef
main:
    movw r0, :lower16:__imp_function
    movt r0, :upper16:__imp_function
    ldr r0, [r0]
    bx r0

    .globl __delayLoadHelper2
    .def __delayLoadHelper2; .scl 2; .type 32; .endef
__delayLoadHelper2:
    bx lr

    .section .text$z, "xr"
    .space 0x1100000
