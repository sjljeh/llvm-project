# RUN: split-file %s %t
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/unmatched.s 2>&1 | FileCheck %s --check-prefix=UNMATCHED
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/mismatch.s 2>&1 | FileCheck %s --check-prefix=MISMATCH
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/interleaved.s 2>&1 | FileCheck %s --check-prefix=INTERLEAVED

# UNMATCHED: error: explicit !lituse sequence has no preceding literal
# MISMATCH: error: mismatched explicit !lituse sequence number
# INTERLEAVED: error: mismatched explicit !lituse sequence number

#--- unmatched.s
addq $1,$2,$3 !lituse_base!1

#--- mismatch.s
ldq $1,foo($29) !literal!1
addq $1,$2,$3 !lituse_base!2

#--- interleaved.s
ldq $1,foo($29) !literal!1
ldq $2,bar($29) !literal!2
addq $1,$2,$3 !lituse_base!1
