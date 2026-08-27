; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -stop-after=finalize-isel < %s -o - | FileCheck %s --check-prefix=MIR
; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj < %s | llvm-readobj --relocations - | FileCheck %s --check-prefix=OBJ
; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj < %s | llvm-readobj --hex-dump=.text - | FileCheck %s --check-prefix=HEX

declare ptr @malloc(i64)
declare i64 @callee(ptr, i64, i64, i64, i64, i64)
declare i64 @callee8(i64, i64, i64, i64, i64, i64, i64, i64)
declare i32 @getwindowlong(ptr, i32)

@G = dso_local global i32 0

define ptr @f() {
entry:
  %p = call ptr @malloc(i64 8)
  ret ptr %p
}

define i64 @call_args(ptr %p) {
entry:
  %v = call i64 @callee(ptr %p, i64 1, i64 2, i64 3, i64 4, i64 5)
  ret i64 %v
}

define i64 @call_stack_args() {
entry:
  %v = call i64 @callee8(i64 1, i64 2, i64 3, i64 4, i64 5, i64 6, i64 7, i64 8)
  ret i64 %v
}

define i32 @negative_i32_arg(ptr %hwnd) {
entry:
  %v = call i32 @getwindowlong(ptr %hwnd, i32 -6)
  ret i32 %v
}

define i64 @bic_imm(i64 %x) {
entry:
  %v = and i64 %x, -2
  ret i64 %v
}

define i64 @ornot_imm(i64 %x) {
entry:
  %v = or i64 %x, -2
  ret i64 %v
}

define ptr @global_addr() {
entry:
  ret ptr @G
}

define i64 @div64(i64 %a, i64 %b) {
entry:
  %q = sdiv i64 %a, %b
  ret i64 %q
}

; MIR-LABEL: name: f
; MIR: BSR @malloc

; MIR-LABEL: name: call_args
; MIR: $r16 = COPY
; MIR: $r17 = COPY
; MIR: $r18 = COPY
; MIR: $r19 = COPY
; MIR: $r20 = COPY
; MIR: $r21 = COPY
; MIR: BSR @callee
; MIR-SAME: implicit $r16
; MIR-SAME: implicit $r17
; MIR-SAME: implicit $r18
; MIR-SAME: implicit $r19
; MIR-SAME: implicit $r20
; MIR-SAME: implicit $r21

; MIR-LABEL: name: call_stack_args
; MIR: ADJUSTSTACKUP 16
; MIR-DAG: STQ {{.*}}, 0,
; MIR-DAG: STQ {{.*}}, 8,
; MIR: BSR @callee8
; MIR: ADJUSTSTACKDOWN 16

; MIR-LABEL: name: negative_i32_arg
; MIR: LDA -6,
; MIR: $r17 = COPY
; MIR: BSR @getwindowlong

; MIR-LABEL: name: bic_imm
; MIR: BICi

; MIR-LABEL: name: ornot_imm
; MIR: ORNOTi

; MIR-LABEL: name: global_addr
; MIR: LDAHr @G
; MIR: LDAr @G

; MIR-LABEL: name: div64
; MIR: LDAHr &_OtsDivide64
; MIR: LDAr &_OtsDivide64
; MIR: $r16 = COPY
; MIR: $r17 = COPY
; MIR: JSR
; MIR-SAME: implicit $r16
; MIR-SAME: implicit $r17

; OBJ: IMAGE_REL_ALPHA_BRADDR malloc
; OBJ: IMAGE_REL_ALPHA_BRADDR callee
; OBJ: IMAGE_REL_ALPHA_BRADDR callee8
; OBJ: IMAGE_REL_ALPHA_BRADDR getwindowlong
; OBJ-NOT: IMAGE_REL_ALPHA_REFLO malloc
; OBJ-NOT: IMAGE_REL_ALPHA_REFLO callee
; OBJ: IMAGE_REL_ALPHA_REFHI _OtsDivide64
; OBJ: IMAGE_REL_ALPHA_PAIR
; OBJ: IMAGE_REL_ALPHA_REFLO _OtsDivide64

; Unresolved branch relocations must not encode a -4 addend in the object.
; HEX: 000040d3

; The VC4 OTS divide helpers use the normal call link register.
; HEX: 00405b6b
