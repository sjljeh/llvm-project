# REQUIRES: riscv

# RUN: split-file %s %t.dir
# RUN: llvm-dlltool -m riscv64 -d %t.dir/lib.def -l %t.dir/lib.lib
# RUN: llvm-mc -triple riscv64-w64-windows-gnu -filetype=obj %t.dir/main.s -o %t.obj
# RUN: lld-link -machine:riscv64 -entry:main -subsystem:console -out:%t.exe %t.obj %t.dir/lib.lib
# RUN: llvm-objdump -d --no-print-imm-hex --no-show-raw-insn -M no-aliases %t.exe | FileCheck %s
# RUN: llvm-readobj --coff-imports %t.exe | FileCheck %s --check-prefix=IMPORTS

# The import thunk loads the IAT slot PC-relatively and jumps through it.
# CHECK:      140001000: auipc ra, 0
# CHECK-NEXT: 140001004: jalr ra, 12(ra)
# CHECK-NEXT: 140001008: jalr zero, 0(ra)
# CHECK-NEXT: 14000100c: auipc t0, 1
# CHECK-NEXT: 140001010: ld t0, 44(t0)
# CHECK-NEXT: 140001014: jalr zero, 0(t0)

# IMPORTS:      Import {
# IMPORTS-NEXT:   Name: lib.dll
# IMPORTS-NEXT:   ImportLookupTableRVA: 0x2028
# IMPORTS-NEXT:   ImportAddressTableRVA: 0x2038
# IMPORTS-NEXT:   Symbol: func (0)
# IMPORTS-NEXT: }

#--- lib.def
LIBRARY lib.dll
EXPORTS
  func

#--- main.s
.text
.globl main
main:
  call func
  ret
