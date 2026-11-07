// REQUIRES: arm
// RUN: llvm-mc -filetype=obj -triple=thumbv7-windows %s -o %t.obj
// RUN: lld-link -entry:main -subsystem:console %t.obj -out:%t.exe -map -verbose 2>&1 | FileCheck -check-prefix=VERBOSE %s
// RUN: llvm-objdump --no-print-imm-hex -d %t.exe --start-address=0x401000 --stop-address=0x401026 | FileCheck --check-prefix=MAIN %s
// RUN: llvm-objdump --no-print-imm-hex -d %t.exe --start-address=0x501028 --stop-address=0x50103a | FileCheck --check-prefix=FUNC1 %s
// RUN: llvm-objdump --no-print-imm-hex -d %t.exe --start-address=0x60103c | FileCheck --check-prefix=FUNC2 %s
// RUN: llvm-readobj --coff-basereloc %t.exe | FileCheck --check-prefix=BASEREL %s

// Like link.exe, thunks are only added for branches that are out of range
// (more passes add more if needed) and are word aligned.
// VERBOSE: Added 3 thunks with margin 0 in {{.*}} passes

    .syntax unified
    .globl main
    .globl func1
    .text
main:
    bne func1
    bne func2
    // This should reuse the same thunk as func1 above
    bne func1_alias
    bx lr
    .section .text$a, "xr"
    .space 0x100000
    .section .text$b, "xr"
func1:
func1_alias:
    // This shouldn't reuse the func2 thunk from above, since it is out
    // of range.
    bne func2
    bx lr
    .section .text$c, "xr"
    .space 0x100000
    .section .text$d, "xr"
func2:
// Test using string tail merging. This is irrelevant to the thunking itself,
// but running multiple passes of assignAddresses() calls finalizeAddresses()
// multiple times; check that MergeChunk handles this correctly.
    movw r0, :lower16:"??_C@string1"
    movt r0, :upper16:"??_C@string1"
    movw r1, :lower16:"??_C@string2"
    movt r1, :upper16:"??_C@string2"
    bx lr

    .section .rdata,"dr",discard,"??_C@string1"
    .globl "??_C@string1"
"??_C@string1":
    .asciz "foobar"
    .section .rdata,"dr",discard,"??_C@string2"
    .globl "??_C@string2"
"??_C@string2":
    .asciz "bar"

// MAIN:    401000:       f040 8006       bne.w   0x401010 <.text+0x10>
// MAIN:    401004:       f040 800a       bne.w   0x40101c <.text+0x1c>
// MAIN:    401008:       f040 8002       bne.w   0x401010 <.text+0x10>
// MAIN:    40100c:       4770            bx      lr
// Like link.exe's, the thunks load the absolute address of the target, with
// the Thumb bit set, and need base relocations.
// func1 thunk
// MAIN:    401010:       f241 0c29       movw    r12, #4137
// MAIN:    401014:       f2c0 0c50       movt    r12, #80
// MAIN:    401018:       4760            bx      r12
// func2 thunk
// MAIN:    40101c:       f241 0c3d       movw    r12, #4157
// MAIN:    401020:       f2c0 0c60       movt    r12, #96
// MAIN:    401024:       4760            bx      r12

// FUNC1:   501028:       f040 8002       bne.w   0x501030 <.text+0x100030>
// FUNC1:   50102c:       4770            bx      lr
// func2 thunk
// FUNC1:   501030:       f241 0c3d       movw    r12, #4157
// FUNC1:   501034:       f2c0 0c60       movt    r12, #96
// FUNC1:   501038:       4760            bx      r12

// FUNC2:   60103c:       f242 0000       movw    r0, #8192
// FUNC2:   601040:       f2c0 0060       movt    r0, #96
// FUNC2:   601044:       f242 0103       movw    r1, #8195
// FUNC2:   601048:       f2c0 0160       movt    r1, #96
// FUNC2:   60104c:       4770            bx      lr

// BASEREL:      Type: ARM_MOV32(T)
// BASEREL-NEXT: Address: 0x1010
// BASEREL:      Type: ARM_MOV32(T)
// BASEREL-NEXT: Address: 0x101C
// BASEREL:      Type: ARM_MOV32(T)
// BASEREL-NEXT: Address: 0x101030
