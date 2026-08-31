; RUN: llc -mtriple=alpha-unknown-linux-gnu -filetype=asm %s -o - | FileCheck %s --check-prefix=ASM
; RUN: llc -mtriple=alpha-unknown-linux-gnu -filetype=obj %s -o - | llvm-readobj --symbols --relocations - | FileCheck %s --check-prefix=OBJ

@global = global i64 1, align 8
@local_tls = internal thread_local(localexec) global i64 2, align 8
@ie_tls = external thread_local(initialexec) global i64

define internal void @padding_callee() noinline {
  ret void
}

define internal i64 @local_callee(i64 %x) noinline {
; ASM-LABEL: local_callee:
; ASM-NEXT: {{[[:space:]]}}.cfi_startproc
; ASM-NEXT: {{[[:space:]]}}.usepv local_callee,std
; ASM: ldah $29,0($27){{.*}}!gpdisp!1
; ASM-NEXT: lda $29,0($29){{.*}}!gpdisp!1
  %v = add i64 %x, 3
  ret i64 %v
}

define ptr @address() {
; ASM-LABEL: address:
; ASM: .usepv address,std
; ASM: ldq $0,global($29){{.*}}!literal
  ret ptr @global
}

define i64 @call_local(i64 %x) {
; ASM-LABEL: call_local:
; ASM: .usepv call_local,std
; ASM: ldq $27,local_callee($29){{.*}}!literal
; ASM: jsr $26,($27),0{{.*}}!lituse_jsr
  %v = call i64 @local_callee(i64 %x)
  ret i64 %v
}

define i64 @load_local_tls() {
; ASM-LABEL: load_local_tls:
; ASM: call_pal 0x9e
; ASM-NEXT: ldah $0,local_tls($0){{.*}}!tprelhi
; ASM-NEXT: lda $0,local_tls($0){{.*}}!tprello
  %v = load i64, ptr @local_tls, align 8
  ret i64 %v
}

define i64 @load_ie_tls() {
; ASM-LABEL: load_ie_tls:
; ASM: ldq ${{[0-9]+}},ie_tls($29){{.*}}!gottprel
; ASM: call_pal 0x9e
  %v = load i64, ptr @ie_tls, align 8
  ret i64 %v
}

; OBJ: R_ALPHA_GPDISP - 0x4
; OBJ: R_ALPHA_LITERAL global 0x0
; OBJ: R_ALPHA_LITERAL local_callee 0x0
; OBJ: R_ALPHA_LITUSE - 0x3
; OBJ: R_ALPHA_TPRELHI local_tls 0x0
; OBJ: R_ALPHA_TPRELLO local_tls 0x0
; OBJ: R_ALPHA_GOTTPREL ie_tls 0x0
; OBJ: Name: address
; OBJ: Other [ (0x88)
