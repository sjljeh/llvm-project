# REQUIRES: x86
# RUN: split-file %s %t.dir && cd %t.dir

# Like link.exe, list delay-loaded imports in the order they were first
# referenced, except that in lists of 8 or more the second to last one moves
# to second place (in shorter lists the first and last ones are swapped).

# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc main.s -o main.obj
# RUN: llvm-lib -machine:x64 -def:dd.def -out:dd.lib
# RUN: lld-link -entry:main -subsystem:console main.obj dd.lib -delayload:dd.dll -out:out.exe
# RUN: llvm-readobj --coff-imports out.exe | FileCheck %s

# CHECK:      DelayImport {
# CHECK:        Name: dd.dll
# CHECK:        BoundDelayImportTable: 0x{{[1-9A-F][0-9A-F]*}}
# CHECK:        Symbol: fna
# CHECK:        Symbol: fnh
# CHECK:        Symbol: fnb
# CHECK:        Symbol: fnc
# CHECK:        Symbol: fnd
# CHECK:        Symbol: fne
# CHECK:        Symbol: fnf
# CHECK:        Symbol: fng
# CHECK:        Symbol: fni

#--- main.s
        .globl main
main:
        call fna
        call fnb
        call fnc
        call fnd
        call fne
        call fnf
        call fng
        call fnh
        call fni
        ret

        .globl __delayLoadHelper2
__delayLoadHelper2:
        ret

#--- dd.def
LIBRARY dd.dll
EXPORTS
fna
fnb
fnc
fnd
fne
fnf
fng
fnh
fni
