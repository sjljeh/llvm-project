; REQUIRES: powerpc-registered-target
; RUN: llc -mtriple=powerpcle-pc-windows-msvc \
; RUN:   -ppc-min-jump-table-entries=4 -filetype=obj < %s -o %t.obj
; RUN: llvm-objdump -d --triple=powerpcle-pc-windows --no-show-raw-insn \
; RUN:   %t.obj | FileCheck %s

; A function descriptor occupies the first eight bytes of .rdata, so the
; section-relative jump-table addend is nonzero. The REFHI instruction must
; contain the high-adjusted part of that addend, not its low part.

; CHECK:      li [[TABLE:[0-9]+]], 8
; CHECK:      addis [[TABLE]], [[TABLE]], 0
; CHECK:      lwzx [[TARGET:[0-9]+]], [[TABLE]], {{[0-9]+}}
; CHECK:      mtctr [[TARGET]]
; CHECK-NEXT: bctr

define i32 @jump_table(i32 %value) {
entry:
  switch i32 %value, label %exit [
    i32 1, label %case1
    i32 2, label %case2
    i32 3, label %case3
    i32 4, label %case4
  ]

case1:
  tail call void asm sideeffect "", ""()
  br label %exit

case2:
  tail call void asm sideeffect "", ""()
  br label %exit

case3:
  tail call void asm sideeffect "", ""()
  br label %exit

case4:
  tail call void asm sideeffect "", ""()
  br label %exit

exit:
  ret i32 0
}
