# REQUIRES: alpha-registered-target
# RUN: llvm-mca -mtriple=alpha-pc-windows-msvc -mcpu=ev4 -iterations=1 \
# RUN:   -instruction-info %s | FileCheck %s --check-prefix=EV4
# RUN: llvm-mca -mtriple=alpha-pc-windows-msvc -mcpu=ev5 -iterations=1 \
# RUN:   -instruction-info %s | FileCheck %s --check-prefix=EV5
# RUN: llvm-mca -mtriple=alpha-pc-windows-msvc -mcpu=ev6 -iterations=1 \
# RUN:   -instruction-info %s | FileCheck %s --check-prefix=EV6

# EV4: Dispatch Width:    2
# EV4: {{1 +1 +0\.25.*addq}}
# EV4: {{1 +3 +0\.50.*ldq}}
# EV4: {{1 +7 +1\.00.*mulq}}

# EV5: Dispatch Width:    4
# EV5: {{1 +1 +0\.25.*addq}}
# EV5: {{1 +2 +0\.50.*ldq}}
# EV5: {{1 +7 +1\.00.*mulq}}

# EV6: Dispatch Width:    4
# EV6: {{1 +1 +0\.25.*addq}}
# EV6: {{1 +3 +0\.50.*ldq}}
# EV6: {{1 +7 +1\.00.*mulq}}

        addq $1,$2,$3
        ldq $4,0($5)
        mulq $6,$7,$8
