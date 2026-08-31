; RUN: llc -mtriple=alpha-pc-windows-msvc -mattr=+taso < %s | FileCheck %s

declare i64 @llvm.ctpop.i64(i64)

define i64 @ev6_popcnt(i64 %x) #0 {
; CHECK-LABEL: ev6_popcnt:
; CHECK-NOT: CTPOP
; CHECK: mulq
; CHECK: ret
  %r = call i64 @llvm.ctpop.i64(i64 %x)
  ret i64 %r
}

define i64 @ev67_popcnt(i64 %x) #1 {
; CHECK-LABEL: ev67_popcnt:
; CHECK: CTPOP $16,$0
; CHECK-NEXT: ret
  %r = call i64 @llvm.ctpop.i64(i64 %x)
  ret i64 %r
}

define i64 @feature_popcnt(i64 %x) #2 {
; CHECK-LABEL: feature_popcnt:
; CHECK: CTPOP $16,$0
; CHECK-NEXT: ret
  %r = call i64 @llvm.ctpop.i64(i64 %x)
  ret i64 %r
}

attributes #0 = { "target-cpu"="ev6" }
attributes #1 = { "target-cpu"="ev67" }
attributes #2 = { "target-cpu"="ev6" "target-features"="+cix,+taso" }
