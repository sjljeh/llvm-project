# REQUIRES: x86-registered-target
# RUN: not llvm-mc -triple=x86_64-unknown-linux %s 2>&1 | FileCheck %s

        .struct -1
        .struct undefined

# CHECK: error: '.struct' offset must be non-negative
# CHECK: error: expected absolute expression
