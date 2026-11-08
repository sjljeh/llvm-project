# RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %s -o %t.obj
# RUN: llvm-readobj --relocations --hex-dump=.text %t.obj | FileCheck %s

# A constant split across ldah and the sign-extending lda is resolved by the
# assembler, with the high half rounded to compensate.

        .set    neg, -20
        .set    big, 0x18000
        .text
        ldah    $0, neg($31)
        lda     $0, neg($0)
        ldah    $1, big($31)
        lda     $1, big($1)

# CHECK:      Relocations [
# CHECK-NEXT: ]
# ldah $0,0($31); lda $0,-20($0); ldah $1,2($31); lda $1,-32768($1)
# CHECK:      0x00000000 00001f24 ecff0020 02003f24 00802120
