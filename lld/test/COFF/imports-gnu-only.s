# REQUIRES: x86
#
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-head.s -filetype=obj -o %t-dabcdh.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-func.s -filetype=obj -o %t-dabcds00000.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-tail.s -filetype=obj -o %t-dabcdt.o
# RUN: rm -f %t-implib.a
# RUN: llvm-ar rcs %t-implib.a %t-dabcdh.o %t-dabcds00000.o %t-dabcdt.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %s -filetype=obj -o %t.obj
# RUN: lld-link -out:%t.exe -entry:main -subsystem:console \
# RUN:   %t.obj %t-implib.a
# RUN: llvm-objdump -s %t.exe | FileCheck --check-prefix=DATA %s

        .text
        .global main
main:
        call func
        ret

# Check that the linker inserted the null terminating import descriptor,
# even if there were no normal import libraries, only gnu ones.

# DATA: Contents of section .rdata:
# The import address table comes first, as in link.exe.
# DATA-NEXT:  140002000 60200000 00000000 00000000 00000000
# The import descriptor at 0x2028, then the null terminator descriptor.
# DATA-NEXT:  140002010
# DATA-NEXT:  140002020 00000000 00000000 50200000 00000000
# DATA-NEXT:  140002030 00000000 6b200000 00200000 00000000
# DATA-NEXT:  140002040 00000000 00000000 00000000 00000000
# Import lookup table.
# DATA-NEXT:  140002050 60200000 00000000 00000000 00000000
