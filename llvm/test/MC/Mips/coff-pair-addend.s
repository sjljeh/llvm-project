# RUN: llvm-mc -triple=mipsel-windows -filetype=obj %s -o - \
# RUN:   | obj2yaml | FileCheck %s

        .text
        .set noat
        lui $1, %hi(target+32767)
        addiu $1, $1, %lo(target+32767)
        .extern target

# CHECK:      Relocations:
# CHECK-NEXT:   - VirtualAddress:  0
# CHECK-NEXT:     SymbolName:      target
# CHECK-NEXT:     Type:            IMAGE_REL_MIPS_REFHI
# CHECK-NEXT:   - VirtualAddress:  0
# CHECK-NEXT:     SymbolTableIndex: 32767
# CHECK-NEXT:     Type:            IMAGE_REL_MIPS_PAIR
# CHECK-NEXT:   - VirtualAddress:  4
# CHECK-NEXT:     SymbolName:      target
# CHECK-NEXT:     Type:            IMAGE_REL_MIPS_REFLO
