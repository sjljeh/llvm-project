# REQUIRES: arm, x86
# RUN: split-file %s %t.dir && cd %t.dir

# Like link.exe, word-align each ARM import thunk.
# RUN: llvm-mc -filetype=obj -triple=thumbv7-windows-msvc arm-thunk.s -o arm-thunk.obj
# RUN: llvm-lib -machine:arm -def:imp.def -out:imp-arm.lib
# RUN: lld-link -entry:start -subsystem:console arm-thunk.obj imp-arm.lib -out:arm-thunk.exe -map:arm-thunk.map
# RUN: FileCheck --check-prefix=ARM-THUNK %s < arm-thunk.map
# ARM-THUNK:      0001:00000000 start
# ARM-THUNK-NEXT: 0001:00000008 func
# ARM-THUNK-NEXT: 0001:00000014 func2

# On ARM, link.exe leaves a word-aligned (empty) import thunk group once an
# import library member is loaded, even if no thunk is needed.
# RUN: llvm-mc -filetype=obj -triple=thumbv7-windows-msvc arm-imp.s -o arm-imp.obj
# RUN: lld-link -entry:start -subsystem:console arm-imp.obj imp-arm.lib -out:arm-imp.exe
# RUN: llvm-readobj --sections arm-imp.exe | FileCheck --check-prefix=ARM-IMP %s
# ARM-IMP:      Name: .text
# ARM-IMP-NEXT: VirtualSize: 0x4

# When all of the loaded imports are unreferenced, link.exe still lays out the
# empty import groups at the end of .rdata, aligned like the lookup table.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc x64-dead.s -o x64-dead.obj
# RUN: llvm-lib -machine:x64 -def:imp.def -out:imp-x64.lib
# RUN: lld-link -entry:start -subsystem:console x64-dead.obj imp-x64.lib -out:x64-dead.exe
# RUN: llvm-readobj --sections --coff-imports x64-dead.exe | FileCheck --check-prefix=X64-DEAD %s
# X64-DEAD:      Name: .rdata
# X64-DEAD-NEXT: VirtualSize: 0x20
# X64-DEAD-NOT:  Import {

#--- imp.def
LIBRARY foo.dll
EXPORTS
func
func2

#--- arm-thunk.s
        .text
        .globl start
        .thumb_func
start:
        nop
        nop
        bx lr

        .section .rdata,"dr"
        .long func
        .long func2

#--- arm-imp.s
        .text
        .globl start
        .thumb_func
start:
        bx lr

        .section .rdata,"dr"
        .long __imp_func

#--- x64-dead.s
        .text
        .globl start
start:
        ret

        .section .rdata,"dr"
        .byte 1

        .section .rdata,"dr",discard,unused
        .globl unused
unused:
        .quad func
