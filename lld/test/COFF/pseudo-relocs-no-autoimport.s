# REQUIRES: x86

# RUN: llvm-mc -triple=x86_64-windows-gnu %s -filetype=obj -o %t.obj
# RUN: lld-link -lldmingw -auto-import:no -out:%t.exe -entry:main %t.obj

# RUN: llvm-objdump -s %t.exe | FileCheck --check-prefix=CONTENTS %s

# Runtime pseudo relocations stay enabled when auto-import is disabled, so the
# list pointers must still point at an (empty) in-image table rather than at
# absolute zero.
# CONTENTS: Contents of section .data:
# CONTENTS:  140003000 00200040 01000000 00200040 01000000

    .global main
    .text
main:
    retq
    .data
relocs:
    .quad __RUNTIME_PSEUDO_RELOC_LIST__
    .quad __RUNTIME_PSEUDO_RELOC_LIST_END__
