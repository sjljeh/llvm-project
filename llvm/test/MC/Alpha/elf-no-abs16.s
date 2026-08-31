# RUN: not llvm-mc -triple=alpha-unknown-linux-gnu -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

# The Alpha ELF ABI has no absolute 16-bit relocation.  Do not silently emit
# R_ALPHA_NONE for an unresolved .short.
# CHECK: LLVM ERROR: Alpha ELF has no 16-bit absolute relocation

.data
.short external_symbol
