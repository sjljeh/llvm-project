# REQUIRES: powerpc-registered-target
# RUN: not llvm-mc -triple=powerpcle-pc-windows %s 2>&1 | FileCheck %s

        .text
symbol:
        .ualong [unknown]symbol
        .uashort [secoff]symbol
        .ualong [secnum]symbol
        lwz r.3,[secoff]symbol(r.2)
        .function
        .function symbol, extra
        .znop
        .znop symbol, extra

# CHECK: error: unknown PASM relocation modifier 'unknown'
# CHECK: error: '[secoff]' requires a 4-byte directive
# CHECK: error: '[secnum]' requires a 2-byte directive
# CHECK: error: unknown PASM relocation modifier 'secoff'
# CHECK: error: expected identifier in '.function' directive
# CHECK: error: unexpected token in '.function' directive
# CHECK: error: expected identifier in '.znop' directive
# CHECK: error: unexpected token in '.znop' directive
