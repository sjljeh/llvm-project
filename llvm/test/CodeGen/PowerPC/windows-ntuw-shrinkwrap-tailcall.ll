; NTUW lets the prologue sit off the entry path (shrink-wrapping) and an
; epilogue end in a branch to another function (a tail call). Code outside the
; frame after the prologue is described by a scope with the entry rules. NT
; 4.0 tables allow neither. Tail calls stay within the module, whose functions
; share the caller's TOC.
; RUN: llc -mtriple=powerpcle-pc-windows-msvc19.33 -O2 < %s | FileCheck %s
; RUN: llc -mtriple=powerpcle-pc-windows-msvc19.33 -O2 -filetype=obj < %s -o %t.obj
; RUN: %python %llvm_src_root/utils/ntuw-check.py %t.obj | FileCheck %s --check-prefix=EVERYPC
; A record that is full continues in a chained record (UNW_ChainInfo), which
; starts from the parent's body state; force a split after every scope.
; RUN: llc -mtriple=powerpcle-pc-windows-msvc19.33 -O2 -ntuw-max-epilogue-scopes=1 \
; RUN:   -filetype=obj < %s -o %t.chain.obj
; RUN: %python %llvm_src_root/utils/ntuw-check.py -v %t.chain.obj \
; RUN:   | FileCheck %s --check-prefix=CHAIN
; RUN: llvm-readobj --unwind %t.chain.obj | FileCheck %s --check-prefix=CHAIN-OBJ
; RUN: llc -mtriple=powerpcle-pc-windows-msvc12.0 -O2 < %s \
; RUN:   | FileCheck %s --check-prefix=LEGACY

; CHECK-LABEL: ..early:
; CHECK-NEXT:  .seh_proc ..early
; CHECK:         cmplwi 3, 0
; CHECK-NEXT:    beq 0, [[EXIT:\.LBB[0-9_]+]]
; CHECK:         mflr 0
; CHECK:         stwu 1, -80(1)
; CHECK-NEXT:    .seh_set_cfa r1, 80
; CHECK-NEXT:    .seh_endprologue
; CHECK:         .seh_endepilogue
; CHECK-NEXT:  [[EXIT]]:
; CHECK-NEXT:    .seh_startepilogue
; CHECK-NEXT:    .seh_set_cfa r1, 0
; CHECK-NEXT:    .seh_same_gpr r29
; CHECK-NEXT:    .seh_same_gpr r30
; CHECK-NEXT:    .seh_same_spr lr
; CHECK-NEXT:    li 3, -1
; CHECK-NEXT:    blr
; CHECK-NEXT:    .seh_endepilogue

; CHECK-LABEL: ..tail:
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    mtlr 0
; CHECK-NEXT:    .seh_same_spr lr
; CHECK-NEXT:    addi 1, 1, 64
; CHECK-NEXT:    .seh_set_cfa r1, 0
; CHECK-NEXT:    b ..local
; CHECK-NEXT:    .seh_endepilogue

; EVERYPC: 4 functions, 0 bad

; CHAIN: ..early [{{.*}}) prologend={{.*}} chained=1: ok
; CHAIN: 5 functions, 0 bad

; CHAIN-OBJ:      Flags: 0x0
; CHAIN-OBJ:      Flags: 0x4
; CHAIN-OBJ-NEXT: FunctionLength:
; CHAIN-OBJ-NEXT: PrologEndOffset: 0x0
; CHAIN-OBJ:      Chained {
; CHAIN-OBJ-NEXT:   BeginAddress: .text+0x0
; CHAIN-OBJ-NEXT:   UnwindData: .xdata+0x0
; CHAIN-OBJ-NEXT: }

; LEGACY-LABEL: ..early:
; LEGACY:       stwu 1, -80(1)
; LEGACY:       cmplwi 3, 0
; LEGACY-LABEL: ..tail:
; LEGACY:       bl ..local
; LEGACY:       blr

declare void @g(i32)
declare i32 @h(i32)
declare double @gd(double)

; The frame is set up only on the path that calls; the early return runs in
; the entry state.
define i32 @early(ptr %p) {
entry:
  %null = icmp eq ptr %p, null
  br i1 %null, label %exit, label %work

work:
  %v = load i32, ptr %p
  %a = call i32 @h(i32 %v)
  %b = call i32 @h(i32 %a)
  %s = add i32 %a, %b
  call void @g(i32 %s)
  %m = mul i32 %a, %b
  br label %exit

exit:
  %r = phi i32 [ -1, %entry ], [ %m, %work ]
  ret i32 %r
}

; Shrink-wrapped with a floating-point nonvolatile.
define double @fpe(double %x, i32 %k) {
entry:
  %z = icmp eq i32 %k, 0
  br i1 %z, label %exit, label %work

work:
  %y = call double @gd(double %x)
  %w = call double @gd(double %y)
  %p = fmul double %y, %w
  %q = fadd double %p, %x
  br label %exit

exit:
  %r = phi double [ %x, %entry ], [ %q, %work ]
  ret double %r
}

; The epilogue releases the frame and branches to the callee.
define i32 @tail(i32 %x) {
  %a = call i32 @h(i32 %x)
  %b = add i32 %a, 1
  %c = tail call i32 @local(i32 %b)
  ret i32 %c
}

define internal i32 @local(i32 %x) noinline {
  %a = call i32 @h(i32 %x)
  %b = add i32 %a, 7
  ret i32 %b
}
