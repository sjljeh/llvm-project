; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,-relax -target-abi=lp64 -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
;
; A C SEH function may use _alloca. SP then moves in the body, so the parent
; keeps its CFA on s0, addresses spill slots through s0 and rebuilds SP from s0
; in its epilogue. A cleanup funclet runs on its own fixed frame: it releases
; that frame from SP and must not rebuild SP from the parent frame in s0.

target triple = "riscv64-w64-windows-gnu"

declare i32 @__C_specific_handler(...)
declare void @use(ptr)
declare void @cleanup(i32)
declare i64 @value(i64)
declare void @sink(i64, i64, i64, i64, i64, i64, i64, i64)
declare void @llvm.localescape(...)
declare ptr @llvm.localrecover(ptr, ptr, i32 immarg)
declare ptr @llvm.localaddress()

; CHECK-LABEL: alloca_except_finally:
; CHECK:         addi s0, sp, {{[0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x8, 0
; CHECK:         .seh_endprologue
; CHECK:         mv sp, {{[a-z0-9]+}}
; CHECK:         call use
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, s0, -{{[0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x2, {{[0-9]+}}
; CHECK:         .seh_endepilogue
; CHECK:       "?dtor${{[0-9]+}}@?0?alloca_except_finally@4HA":
; CHECK:         .seh_set_cfa x2, {{[0-9]+}}
; CHECK:         mv s0, a1
; CHECK:         .seh_endprologue
; CHECK:         call fin
; CHECK:         .seh_startepilogue
; CHECK-NOT:     s0, -
; CHECK:         addi sp, sp, {{[0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x2, 0
; CHECK:         .seh_endepilogue
define i32 @alloca_except_finally(i32 %n) personality ptr @__C_specific_handler {
entry:
  %n.addr = alloca i32, align 4
  call void (...) @llvm.localescape(ptr %n.addr)
  store i32 %n, ptr %n.addr, align 4
  %size = sext i32 %n to i64
  %buf = alloca i8, i64 %size, align 16
  invoke void @use(ptr %buf)
          to label %inner unwind label %catch.dispatch

inner:
  %p1 = getelementptr inbounds i8, ptr %buf, i64 1
  invoke void @use(ptr %p1)
          to label %normal.fin unwind label %ehcleanup

normal.fin:
  %fp0 = call ptr @llvm.localaddress()
  invoke void @fin(ptr %fp0)
          to label %done unwind label %catch.dispatch

ehcleanup:
  %cp = cleanuppad within none []
  %fp1 = call ptr @llvm.localaddress()
  invoke void @fin(ptr %fp1) [ "funclet"(token %cp) ]
          to label %cleanup.done unwind label %catch.dispatch

cleanup.done:
  cleanupret from %cp unwind label %catch.dispatch

catch.dispatch:
  %cs = catchswitch within none [label %except] unwind to caller

except:
  %pad = catchpad within %cs [ptr null]
  catchret from %pad to label %except.body

except.body:
  %v = load i32, ptr %n.addr, align 4
  br label %done

done:
  %r = phi i32 [ 0, %normal.fin ], [ %v, %except.body ]
  ret i32 %r
}

define internal void @fin(ptr %frame) {
entry:
  %n.addr = call ptr @llvm.localrecover(ptr @alloca_except_finally, ptr %frame, i32 0)
  %n = load i32, ptr %n.addr, align 4
  call void @cleanup(i32 %n)
  ret void
}

; Values live across the calls outnumber the callee-saved registers, so some
; are spilled after SP moved: their slots must be addressed through s0.
; CHECK-LABEL: alloca_spills:
; CHECK:         .seh_endprologue
; CHECK:         mv sp, {{[a-z0-9]+}}
; CHECK:         sd {{[a-z0-9]+}}, -{{[0-9]+}}(s0)
; CHECK:         ld {{[a-z0-9]+}}, -{{[0-9]+}}(s0)
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, s0, -{{[0-9]+}}
define i64 @alloca_spills(i64 %n) personality ptr @__C_specific_handler {
entry:
  %buf = alloca i8, i64 %n, align 16
  %v0 = call i64 @value(i64 0)
  %v1 = call i64 @value(i64 1)
  %v2 = call i64 @value(i64 2)
  %v3 = call i64 @value(i64 3)
  %v4 = call i64 @value(i64 4)
  %v5 = call i64 @value(i64 5)
  %v6 = call i64 @value(i64 6)
  %v7 = call i64 @value(i64 7)
  %v8 = call i64 @value(i64 8)
  %v9 = call i64 @value(i64 9)
  %v10 = call i64 @value(i64 10)
  %v11 = call i64 @value(i64 11)
  %v12 = call i64 @value(i64 12)
  %v13 = call i64 @value(i64 13)
  invoke void @use(ptr %buf)
          to label %cont unwind label %catch.dispatch

cont:
  call void @sink(i64 %v0, i64 %v1, i64 %v2, i64 %v3, i64 %v4, i64 %v5, i64 %v6, i64 %v7)
  call void @sink(i64 %v8, i64 %v9, i64 %v10, i64 %v11, i64 %v12, i64 %v13, i64 %v0, i64 %v1)
  br label %done

catch.dispatch:
  %cs = catchswitch within none [label %except] unwind to caller

except:
  %pad = catchpad within %cs [ptr null]
  catchret from %pad to label %done

done:
  %r = phi i64 [ %v13, %cont ], [ 0, %except ]
  ret i64 %r
}

; The parent's prologue ends on an s0 CFA; the funclet keeps an SP CFA.
; UNWIND:      RuntimeFunction {
; UNWIND:        PrologCodes [
; UNWIND:          Opcode: SET_CFA
; UNWIND-NEXT:     Register: x2 (2)
; UNWIND:          Opcode: SET_CFA
; UNWIND-NEXT:     Register: x8 (8)
; UNWIND-NEXT:     Operand: 0
; UNWIND:      RuntimeFunction {
; UNWIND:        PrologCodes [
; UNWIND:          Opcode: SET_CFA
; UNWIND-NEXT:     Register: x2 (2)
; UNWIND-NOT:      Operand: 0{{$}}
; UNWIND:        EpilogScopes [
