; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,+v,-relax -target-abi=lp64d -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,+v,-relax -target-abi=lp64d -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
;
; RVUW cannot describe an SP offset that scales with VLENB. A function that
; spills vector registers keeps its CFA on s0 instead, so the scalable part of
; the frame never enters the unwind description, and rebuilds SP from s0 in
; its epilogue. Vector registers are caller-saved: no vector state is unwound.
; A function without vector code keeps its frame-pointer-free frame.

target triple = "riscv64-w64-windows-gnu"

declare void @clobber()

; CHECK-LABEL: vector_spill:
; CHECK:         addi s0, sp, {{[0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x8, 0
; CHECK:         csrr {{[a-z0-9]+}}, vlenb
; CHECK:         sub sp, sp, {{[a-z0-9]+}}
; CHECK:         .seh_endprologue
; CHECK:         vs1r.v
; CHECK:         call clobber
; CHECK:         vl1r.v
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, s0, -{{[0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x2, {{[0-9]+}}
; CHECK:         .seh_endepilogue
define <vscale x 2 x i32> @vector_spill(<vscale x 2 x i32> %a, <vscale x 2 x i32> %b) uwtable {
entry:
  %sum = add <vscale x 2 x i32> %a, %b
  call void @clobber()
  %r = mul <vscale x 2 x i32> %sum, %a
  ret <vscale x 2 x i32> %r
}

; CHECK-LABEL: scalar_only:
; CHECK-NOT:     .seh_set_cfa x8
; CHECK-NOT:     vlenb
; CHECK:         .seh_endprologue
; CHECK-NOT:     .seh_set_cfa x8
; CHECK:         call clobber
; CHECK-NOT:     .seh_set_cfa x8
; CHECK:         .seh_endproc
define i64 @scalar_only(i64 %x) uwtable {
entry:
  call void @clobber()
  %r = add i64 %x, 1
  ret i64 %r
}

; An SEH function with vector spills keeps them in the parent frame, reached
; through s0 from the parent and from its funclets alike; the funclet frame
; stays fixed and allocates no vector area.
; CHECK-LABEL: seh_vector_spill:
; CHECK:         .seh_set_cfa x8, 0
; CHECK:         .seh_endprologue
; CHECK:         sub {{[a-z0-9]+}}, s0, {{[a-z0-9]+}}
; CHECK:         vs1r.v
; CHECK:         call clobber
; CHECK:         vl1r.v
; CHECK:         .seh_startepilogue
; CHECK-NEXT:    addi sp, s0, -{{[0-9]+}}
; CHECK:       "?dtor${{[0-9]+}}@?0?seh_vector_spill@4HA":
; CHECK-NOT:     vlenb
; CHECK:         mv s0, a1
; CHECK-NOT:     vlenb
; CHECK:         .seh_endprologue
; CHECK:         call cleanup
; CHECK:         .seh_startepilogue
; CHECK-NOT:     vlenb
; CHECK:         .seh_endepilogue
declare i32 @__C_specific_handler(...)
declare void @cleanup()
define <vscale x 2 x i32> @seh_vector_spill(<vscale x 2 x i32> %a, <vscale x 2 x i32> %b) uwtable personality ptr @__C_specific_handler {
entry:
  %sum = add <vscale x 2 x i32> %a, %b
  invoke void @clobber()
          to label %cont unwind label %ehcleanup

cont:
  %r = mul <vscale x 2 x i32> %sum, %a
  call void @cleanup()
  ret <vscale x 2 x i32> %r

ehcleanup:
  %cp = cleanuppad within none []
  call void @cleanup() [ "funclet"(token %cp) ]
  cleanupret from %cp unwind to caller
}

; UNWIND:      RuntimeFunction {
; UNWIND:        PrologCodes [
; UNWIND:          Opcode: SET_CFA
; UNWIND-NEXT:     Register: x2 (2)
; UNWIND-NEXT:     Operand: 32
; UNWIND:          Opcode: SET_CFA
; UNWIND-NEXT:     Register: x8 (8)
; UNWIND-NEXT:     Operand: 0
