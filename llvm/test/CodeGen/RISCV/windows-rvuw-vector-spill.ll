; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,+v,-relax -target-abi=lp64d -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=riscv64-w64-windows-gnu -mattr=+m,+a,+f,+d,+c,+v,-relax -target-abi=lp64d -filetype=obj < %s -o %t.obj
; RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=UNWIND
;
; RVUW describes an SP offset that scales with VLENB (SET_CFA_SCALED), so a
; function with RVV stack objects needs no frame pointer: its CFA follows SP
; through the scalable allocation and back. A riscv_vector_cc function
; describes its callee-saved vector registers with SAVE_VR/SAME_VR.

target triple = "riscv64-w64-windows-gnu"

declare void @clobber()

; CHECK-LABEL: vector_spill:
; CHECK-NOT:     s0
; CHECK:         .seh_set_cfa x2, [[SIZE:[0-9]+]]
; CHECK:         csrr {{[a-z0-9]+}}, vlenb
; CHECK:         sub sp, sp, {{[a-z0-9]+}}
; CHECK-NEXT:    .seh_set_cfa_scaled x2, [[SIZE]], 2
; CHECK-NEXT:    .seh_endprologue
; CHECK:         vs1r.v
; CHECK:         call clobber
; CHECK:         vl1r.v
; CHECK:         .seh_startepilogue
; CHECK:         add sp, sp, {{[a-z0-9]+}}
; CHECK-NEXT:    .seh_set_cfa x2, [[SIZE]]
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

; CHECK-LABEL: vector_cc_saves:
; CHECK:         .seh_set_cfa_scaled x2, {{[0-9]+}}, {{[0-9]+}}
; CHECK:         vs1r.v v1,
; CHECK:         .seh_save_vr v1, -{{[0-9]+}}, -{{[0-9]+}}
; CHECK:         .seh_save_vr v24, -{{[0-9]+}}, -{{[0-9]+}}
; CHECK:         .seh_endprologue
; CHECK:         .seh_startepilogue
; CHECK:         vl1r.v v1,
; CHECK:         .seh_same_vr v1
; CHECK:         .seh_same_vr v31
; CHECK-NEXT:    csrr
; CHECK:         .seh_endepilogue
define riscv_vector_cc <vscale x 2 x i32> @vector_cc_saves(<vscale x 2 x i32> %a) uwtable {
entry:
  call void asm sideeffect "", "~{v1},~{v24}"()
  call void @clobber()
  ret <vscale x 2 x i32> %a
}

; UNWIND:      RuntimeFunction {
; UNWIND:        RequiredState: 0x5
; UNWIND:        PrologCodes [
; UNWIND:          Opcode: SET_CFA
; UNWIND-NEXT:     Register: x2 (2)
; UNWIND:          Opcode: SET_CFA_SCALED
; UNWIND-NEXT:     Register: x2 (2)
; UNWIND-NEXT:     Operand: [[#SIZE:]]
; UNWIND-NEXT:     VLENBMultiplier: 2
; UNWIND-NEXT:     OpInfo:
; UNWIND-NEXT:     Slots: 2
