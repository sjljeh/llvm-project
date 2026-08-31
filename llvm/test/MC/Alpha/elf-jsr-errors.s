# RUN: split-file %s %t
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/dest.s 2>&1 | FileCheck %s --check-prefix=DEST
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/pv.s 2>&1 | FileCheck %s --check-prefix=PV
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/hint.s 2>&1 | FileCheck %s --check-prefix=HINT

# DEST: error: jsr destination register must be $26
# PV: error: jsr procedure-value register must be $27
# HINT: error: non-zero jsr hints are unsupported

#--- dest.s
jsr $1,($27),0

#--- pv.s
jsr $26,($1),0

#--- hint.s
jsr $26,($27),1
