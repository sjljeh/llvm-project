; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -stop-after=postrapseudos < %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj < %s | llvm-readobj --hex-dump=.text - | FileCheck %s --check-prefix=OBJ

define i32 @select_reg(i32 %cond, i32 %truev, i32 %falsev) {
entry:
  %cmp = icmp ne i32 %cond, 0
  %sel = select i1 %cmp, i32 %truev, i32 %falsev
  ret i32 %sel
}

define i32 @select_imm_false(i32 %cond, i32 %truev) {
entry:
  %cmp = icmp ne i32 %cond, 0
  %sel = select i1 %cmp, i32 %truev, i32 7
  ret i32 %sel
}

; MIR-LABEL: name: select_reg
; MIR: $r0 = BISr $r18, $r18
; MIR: $r2 = ZAPNOTi killed $r16, 15
; MIR: $r0 = CMOVNEr killed $r2, killed $r17, killed $r0

; MIR-LABEL: name: select_imm_false
; MIR: $r0 = BISr $r17, $r17
; MIR: $r2 = ZAPNOTi killed $r16, 15
; MIR: $r0 = CMOVEQi killed $r2, 7, killed $r0

; OBJ: 00045246 22f6014a c0045144 0180fa6b
; OBJ: 00043146 22f6014a 80f44044 0180fa6b
