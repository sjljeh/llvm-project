// REQUIRES: arm-registered-target
// RUN: llvm-mc -filetype=obj -triple thumbv7-windows-msvc %s -o %t.o
// RUN: llvm-readobj --unwind %t.o | FileCheck --strict-whitespace %s

// The Microsoft-specific 0xEE opcodes that the Windows kernel's kxarmunw.h
// emits for trap entries (PROLOG_PUSH_TRAP_FRAME, PROLOG_PUSH_MACHINE_FRAME,
// PROLOG_PUSH_CONTEXT) and red-zone LR saves. They appear in prologues as well
// as epilogues.

// CHECK:       Function: trap_frame
// CHECK:         Prologue [
// CHECK-NEXT:      0xd4                ; push {r4, lr}
// CHECK-NEXT:      0xee 0x03           ; trap frame
// CHECK-NEXT:    ]
// CHECK:       Function: machine_frame
// CHECK:         Prologue [
// CHECK-NEXT:      0xd4                ; push {r4, lr}
// CHECK-NEXT:      0xee 0x01           ; machine frame
// CHECK-NEXT:    ]
// CHECK:       Function: context
// CHECK:         Prologue [
// CHECK-NEXT:      0xd4                ; push {r4, lr}
// CHECK-NEXT:      0xee 0x02           ; context
// CHECK-NEXT:    ]
// CHECK:       Function: redzone
// CHECK:         Prologue [
// CHECK-NEXT:      0xee 0x04           ; redzone restore lr
// CHECK-NEXT:      0xfd                ; bx <reg>
// CHECK-NEXT:    ]
// CHECK:           Opcodes [
// CHECK-NEXT:        0xee 0x04           ; redzone restore lr
// CHECK-NEXT:        0xfd                ; bx <reg>
// CHECK-NEXT:      ]
// CHECK:       Function: reserved
// CHECK:         Prologue [
// CHECK-NEXT:      0xee 0x07           ; microsoft-specific (type: 7)
// CHECK-NEXT:      0xee 0x10           ; reserved
// CHECK-NEXT:    ]

        .text
        .syntax unified
        .thumb

        .macro frame name, code
        .def \name
        .scl 2
        .type 32
        .endef
        .seh_proc \name
\name:
        .seh_custom 0xee, \code
        push {r4, lr}
        .seh_save_regs {r4, lr}
        .seh_endprologue
        bl other
        .seh_startepilogue
        pop {r4, pc}
        .seh_save_regs {r4, lr}
        .seh_endepilogue
        .seh_endproc
        .endm

        frame trap_frame, 0x03
        frame machine_frame, 0x01
        frame context, 0x02

        .def redzone
        .scl 2
        .type 32
        .endef
        .seh_proc redzone
redzone:
        str lr, [sp, #-8]
        .seh_custom 0xee, 0x04
        .seh_endprologue
        bl other
        .seh_startepilogue
        ldr lr, [sp, #-8]
        .seh_custom 0xee, 0x04
        bx lr
        .seh_nop
        .seh_endepilogue
        .seh_endproc

        .def reserved
        .scl 2
        .type 32
        .endef
        .seh_proc reserved
reserved:
        .seh_custom 0xee, 0x10
        .seh_custom 0xee, 0x07
        .seh_endprologue
        bx lr
        .seh_endproc
