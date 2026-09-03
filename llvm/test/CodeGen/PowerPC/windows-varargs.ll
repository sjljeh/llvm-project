; REQUIRES: powerpc-registered-target
; RUN: llc -verify-machineinstrs -O2 -ppc-asm-full-reg-names < %s | FileCheck %s

target datalayout = "e-m:w-p:32:32-Fn32-i64:64-n32"
target triple = "powerpcle-pc-windows-msvc"

declare void @sink(i32, ...)
declare void @fixed_sink(i32, double, i32)
declare void @llvm.va_start.p0(ptr)
declare ptr @llvm.ptrmask.p0.i32(ptr, i32)

; The doubles stay in f1/f2 and are duplicated into their aligned parameter
; words in r5:r6 and r9:r10. The integer between them is therefore r7, not a
; compacted r5. Arguments beyond the eight-word home area start at SP+56.
;
; CHECK-LABEL: ..call_variadic:
; CHECK:       stwu r1, -80(r1)
; CHECK-DAG:   lfs f1,
; CHECK-DAG:   lfs f2,
; CHECK-DAG:   li r3, 1
; CHECK-DAG:   li r4, 2
; CHECK-DAG:   li r5, 0
; CHECK-DAG:   lis r6, 16392
; CHECK-DAG:   li r7, 4
; CHECK-DAG:   li r9, 0
; CHECK-DAG:   lis r10, 16404
; CHECK-DAG:   stw {{r[0-9]+}}, 56(r1)
; CHECK-DAG:   stw {{r[0-9]+}}, 60(r1)
; CHECK-DAG:   stw {{r[0-9]+}}, 64(r1)
; CHECK-DAG:   stw {{r[0-9]+}}, 68(r1)
; CHECK-DAG:   stw {{r[0-9]+}}, 72(r1)
; CHECK-NOT:   crxor
; CHECK-NOT:   creqv
; CHECK:       bl ..sink
define void @call_variadic() {
entry:
  call void (i32, ...) @sink(i32 1, i32 2, double 3.000000e+00, i32 4,
                              i32 undef, double 5.000000e+00,
                              i64 1234605616436508552, i32 6, i32 7, i32 8)
  ret void
}

; Eight-byte values are aligned to an even parameter-word index. r4 is the
; padding word, the low-address and high-address halves use r5:r6, and the next
; integer uses r7.
;
; CHECK-LABEL: ..call_i64:
; CHECK:       stwu r1, -64(r1)
; CHECK-DAG:   ori r5, {{r[0-9]+}}, 30600
; CHECK-DAG:   ori r6, {{r[0-9]+}}, 13124
; CHECK-DAG:   li r3, 1
; CHECK-DAG:   li r7, 2
; CHECK-NOT:   crxor
; CHECK-NOT:   creqv
; CHECK:       bl ..sink
define void @call_i64() {
entry:
  call void (i32, ...) @sink(i32 1, i32 undef,
                              i64 1234605616436508552, i32 2)
  ret void
}

; Floating variadic arguments continue to use FPRs after the GPR home area is
; exhausted, but their two parameter words must also be written to the stack.
;
; CHECK-LABEL: ..call_stack_double:
; CHECK:       stwu r1, -80(r1)
; CHECK-DAG:   lfs f1,
; CHECK-DAG:   stw {{r[0-9]+}}, 56(r1)
; CHECK-DAG:   stw {{r[0-9]+}}, 60(r1)
; CHECK-DAG:   stw {{r[0-9]+}}, 64(r1)
; CHECK-DAG:   li r3, 0
; CHECK-DAG:   li r4, 1
; CHECK-DAG:   li r5, 2
; CHECK-DAG:   li r6, 3
; CHECK-DAG:   li r7, 4
; CHECK-DAG:   li r8, 5
; CHECK-DAG:   li r9, 6
; CHECK-DAG:   li r10, 7
; CHECK:       bl ..sink
define void @call_stack_double() {
entry:
  call void (i32, ...) @sink(i32 0, i32 1, i32 2, i32 3, i32 4,
                              i32 5, i32 6, i32 7, double 3.000000e+00,
                              i32 9)
  ret void
}

; Fixed floating arguments reserve the same parameter words without requiring
; GPR copies. The following integer is still in positional register r7.
;
; CHECK-LABEL: ..call_fixed:
; CHECK:       lfs f1,
; CHECK-DAG:   li r3, 1
; CHECK-DAG:   li r7, 4
; CHECK:       bl ..fixed_sink
define void @call_fixed() {
entry:
  call void @fixed_sink(i32 1, double 3.000000e+00, i32 4)
  ret void
}

; A variadic callee homes r3-r10 to the caller-provided words at incoming
; SP+24 through SP+52. va_start for one fixed word is incoming SP+28.
;
; CHECK-LABEL: ..read_i64:
; CHECK:       stwu r1, -64(r1)
; CHECK:       addi r11, r1, 92
; CHECK-DAG:   stw r3, 88(r1)
; CHECK-DAG:   stw r4, 92(r1)
; CHECK-DAG:   stw r5, 96(r1)
; CHECK-DAG:   stw r6, 100(r1)
; CHECK-DAG:   stw r7, 104(r1)
; CHECK-DAG:   stw r8, 108(r1)
; CHECK-DAG:   stw r9, 112(r1)
; CHECK-DAG:   stw r10, 116(r1)
; CHECK-NOT:   stfd
; CHECK:       lwz r3, 0({{r[0-9]+}})
; CHECK:       lwz r4, 4({{r[0-9]+}})
define i64 @read_i64(i32 %fixed, ...) {
entry:
  %ap = alloca ptr, align 4
  call void @llvm.va_start.p0(ptr %ap)
  %cur = load ptr, ptr %ap, align 4
  %biased = getelementptr inbounds i8, ptr %cur, i32 7
  %aligned = call ptr @llvm.ptrmask.p0.i32(ptr %biased, i32 -8)
  %next = getelementptr inbounds i8, ptr %aligned, i32 8
  store ptr %next, ptr %ap, align 4
  %value = load i64, ptr %aligned, align 8
  ret i64 %value
}
