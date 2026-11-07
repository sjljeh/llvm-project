// REQUIRES: aarch64
// RUN: split-file %s %t.dir && cd %t.dir

// Like link.exe, load the archive members needed for symbols that are
// already undefined in the order the symbols became undefined, also when
// they are found through the archive's EC symbol map.

// RUN: llvm-mc -filetype=obj -triple=arm64ec-windows a.s -o a.obj
// RUN: llvm-mc -filetype=obj -triple=arm64ec-windows b.s -o b.obj
// RUN: llvm-mc -filetype=obj -triple=arm64ec-windows main.s -o main.obj
// RUN: llvm-mc -filetype=obj -triple=arm64ec-windows %S/Inputs/loadconfig-arm64ec.s -o loadconfig-arm64ec.obj
// RUN: llvm-lib -machine:arm64ec -out:lib.lib a.obj b.obj
// RUN: lld-link -machine:arm64ec -dll -noentry -out:out.dll loadconfig-arm64ec.obj main.obj lib.lib \
// RUN:          -include:#main -map
// RUN: FileCheck %s < out.map

// CHECK:      0001:0000000c       #func_b {{.*}} lib:b.obj
// CHECK-NEXT: 0001:00000010       #func_a {{.*}} lib:a.obj

#--- a.s
        .text
        .globl "#func_a"
        .p2align 2
"#func_a":
        ret

#--- b.s
        .text
        .globl "#func_b"
        .p2align 2
"#func_b":
        ret

#--- main.s
        .text
        .globl "#main"
        .p2align 2
"#main":
        bl "#func_b"
        bl "#func_a"
        ret
