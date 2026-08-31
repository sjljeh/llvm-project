# RUN: split-file %s %t
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/nonadjacent.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NONADJ
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/mismatch.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISMATCH
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/wrong-start.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=START
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/unpaired.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNPAIRED
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/lituse-mismatch.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LITUSE-MISMATCH
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/lituse-unpaired.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LITUSE-UNPAIRED
# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu %t/gpdisp-no-sequence.s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NO-SEQUENCE

# NONADJ: error: expected adjacent lda with matching explicit !gpdisp sequence
# MISMATCH: error: mismatched explicit !gpdisp sequence number
# START: error: explicit !gpdisp sequence must start with ldah
# UNPAIRED: error: unpaired explicit !gpdisp sequence
# LITUSE-MISMATCH: error: mismatched explicit !lituse sequence number
# LITUSE-UNPAIRED: error: explicit !lituse sequence has no preceding literal
# NO-SEQUENCE: error: !gpdisp requires an explicit sequence number

#--- nonadjacent.s
.text
  ldah $29,0($27) !gpdisp!1
  unop
  lda $29,0($29) !gpdisp!1

#--- mismatch.s
.text
  ldah $29,0($27) !gpdisp!1
  lda $29,0($29) !gpdisp!2

#--- wrong-start.s
.text
  lda $29,0($29) !gpdisp!1

#--- unpaired.s
.text
  ldah $29,0($27) !gpdisp!1

#--- lituse-mismatch.s
.text
  ldq $1,target($29) !literal!1
  addq $1,$2,$3 !lituse_base!2

#--- lituse-unpaired.s
.text
  addq $1,$2,$3 !lituse_base!1

#--- gpdisp-no-sequence.s
.text
  ldah $29,0($27) !gpdisp
