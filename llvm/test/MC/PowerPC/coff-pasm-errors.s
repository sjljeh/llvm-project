# REQUIRES: powerpc-registered-target
# RUN: not llvm-mc -triple=powerpcle-pc-windows %s 2>&1 | FileCheck %s

        .text
symbol:
        .ualong [unknown]symbol
        .uashort [secoff]symbol
        .ualong [secnum]symbol

# CHECK: error: unknown PASM relocation modifier 'unknown'
# CHECK: error: '[secoff]' requires a 4-byte directive
# CHECK: error: '[secnum]' requires a 2-byte directive
