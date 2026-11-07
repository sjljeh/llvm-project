// REQUIRES: arm
// RUN: llvm-mc -filetype=obj -triple=thumbv7-windows %s -o %t.obj
// RUN: lld-link -entry:main -subsystem:console %t.obj -out:%t.exe -verbose 2>&1 | FileCheck -check-prefix=VERBOSE %s
// RUN: llvm-objdump --no-print-imm-hex -d %t.exe --start-address=0x403000 --stop-address=0x403008 | FileCheck --check-prefix=FUNC01 %s
// RUN: llvm-objdump --no-print-imm-hex -d %t.exe --start-address=0x404ffa --stop-address=0x405014 | FileCheck --check-prefix=FUNC01-THUNKS %s

// As in link.exe, thunks are added without a margin: the first pass only
// adds thunks for the far_func branches, which pushes some func_within_margin
// branches out of range, so a second pass adds thunks for those after the
// thunks from the first pass are kept.
// VERBOSE: Added {{.*}} thunks with margin 0 in 2 passes

    .syntax unified
    .globl main
    .text
main:
    b  func01
    bx lr

.irp i, 01, 02, 03, 04, 05, 06, 07, 08, 09, 10, 11, 12, 13, 14, 15, 16, 17, 18
    .section .text$\i\()a, "xr"
    .balign 8192
func\i:
    bne far_func\i
    bne func_within_margin\i
    // Originally, the first section is less than 8192 bytes large, and the
    // second one follows almost directly. After adding one thunk after
    // the first section, the second one will move forward by 8192 bytes
    // due to the alignment.
    .space 8192 - 8 - 4

    .section .text$\i\()b, "xr"
    .balign 8192
align\i:
    nop
.endr

    .section .text$999, "xr"
tail:
    .space 0x100000 - 100*1024 - 18*8192*2
    // Initially, these symbols are within range from all the sections above,
    // even when taking the initial margin into account. After adding thunks
    // to all the sections above, some of these are also out of range, forcing
    // running a second pass.
.irp i, 01, 02, 03, 04, 05, 06, 07, 08, 09, 10, 11, 12, 13, 14, 15, 16, 17, 18
func_within_margin\i:
    nop
.endr
    .space 0x100000

    // These are always out of range.
.irp i, 01, 02, 03, 04, 05, 06, 07, 08, 09, 10, 11, 12, 13, 14, 15, 16, 17, 18
far_func\i:
    nop
.endr
    bx lr

// FUNC01: 403000:       f042 8002       bne.w   0x405008 <.text+0x4008>
// FUNC01: 403004:       f041 87fa       bne.w   0x404ffc <.text+0x3ffc>

// Check that we only have two thunks here; the second pass inserts its thunk
// directly after the section, before the thunk from the first pass.

// FUNC01-THUNKS: 404ffa:       0000            movs    r0,  r0
// The instruction above is padding from the .space
// FUNC01-THUNKS: 404ffc:       f24c 0c03       movw    r12, #49155
// FUNC01-THUNKS: 405000:       f2c0 0c50       movt    r12, #80
// FUNC01-THUNKS: 405004:       4760            bx      r12
// FUNC01-THUNKS: 405006:       0000            movs    r0, r0
// FUNC01-THUNKS: 405008:       f24c 0c27       movw    r12, #49191
// FUNC01-THUNKS: 40500c:       f2c0 0c60       movt    r12, #96
// FUNC01-THUNKS: 405010:       4760            bx      r12
// The instruction below is padding from the .balign
// FUNC01-THUNKS: 405012:       0000            movs    r0, r0
