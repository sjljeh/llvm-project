// REQUIRES: alpha-registered-target
//
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -O0 -S \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -O2 -S \
// RUN:   -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -O2 -c \
// RUN:   -o %t.obj %s
// RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -O0 -c \
// RUN:   -o %t.o0.obj %s

// C SEH on NT Alpha: __C_specific_handler's scope tables hold absolute
// addresses, and Windows enters a filter or termination handler with its
// argument in a0, the handler's own address in a1, and the establisher frame
// (the parent's incoming $30, NT's virtual frame pointer) in a2, v0 and t0.
// RtlUnwind enters an __except block with the exception code in v0.

extern void may_fault(void);
extern int inspect_exception(unsigned code, int selector);
extern volatile int sink;

int seh_filter(volatile int *p, int selector) {
  int value = selector + 7;
  __try {
    value += *p;
    may_fault();
  } __except (inspect_exception(__exception_code(), value)) {
    value = -value;
  }
  return value;
}

// IR-LABEL: define dso_local i32 @seh_filter(
// IR-SAME: personality ptr @__C_specific_handler
// IR-LABEL: define internal i32 @"?filt$0@0@seh_filter@@"(ptr noundef %0, ptr noundef %1, ptr noundef %2)
// IR: call ptr @llvm.eh.recoverfp(ptr @seh_filter, ptr %2)

// The NT unwinder reverse-executes the prologue: $26 is stored there, and the
// frame pointer copy comes after every store.
// ASM-LABEL: seh_filter:
// ASM: .seh_handler __C_specific_handler, @unwind, @except
// ASM: lda $30,-48($30)
// ASM-NEXT: stq $15,0($30)
// ASM-NEXT: stq $26,40($30)
// ASM-NEXT: stq $9,32($30)
// ASM-NEXT: bis $30,$30,$15
// ASM-NEXT: .seh_endprologue
// ASM: .Lseh_filter$frame_escape_0 = -24
// ASM: lda $9,24($15)
// ASM: bsr $26,$may_fault..ng
// The epilogue restores $30 first, reloads $15 last, and deallocates in the
// instruction before the return.
// ASM: bis $15,$15,$30
// ASM-NEXT: ldq $9,32($30)
// ASM-NEXT: ldq $26,40($30)
// ASM-NEXT: ldq $15,0($30)
// ASM-NEXT: lda $30,48($30)
// ASM-NEXT: ret
// ASM: .Lseh_filter$parent_frame_mask = -1
// ASM: .Lseh_filter$parent_frame_offset = 0
// ASM: .long {{.*}} # LabelEnd
// ASM-NEXT: .long "?filt$0@0@seh_filter@@" # FilterFunction
// ASM-NEXT: .long .LBB0_4 # ExceptionHandler

// The filter's establisher frame, rounded down to the parent's frame alignment
// ($parent_frame_mask, -1 for a frame that is not realigned), is
// llvm.localaddress.
// ASM-LABEL: "?filt$0@0@seh_filter@@":
// ASM: ldah $0,.Lseh_filter$parent_frame_mask($31)
// ASM-NEXT: lda $0,.Lseh_filter$parent_frame_mask($0)
// ASM-NEXT: and $18,$0,$0
// ASM-NEXT: ldah $1,.Lseh_filter$frame_escape_0($31)
// ASM-NEXT: lda $1,.Lseh_filter$frame_escape_0($1)
// ASM-NEXT: addq $0,$1,$0
// ASM-NEXT: ldl $17,0($0)
// ASM: bsr $26,$inspect_exception..ng

void seh_finally(int n) {
  int local = n + 3;
  __try {
    may_fault();
  } __finally {
    sink = local + __abnormal_termination();
  }
}

// The termination funclet runs on its own frame and points $15 at its
// parent's frame body, from the establisher frame in t0.
// ASM-LABEL: "?dtor$4@?0?seh_finally@4HA":
// ASM: lda $30,-32($30)
// ASM-NEXT: stq $15,0($30)
// ASM-NEXT: stq $26,24($30)
// ASM-NEXT: lda $15,-32($1)
// ASM-NEXT: .seh_endprologue
// ASM: lda $1,32($15)
// ASM-NOT: bis $15,$15,$30
// ASM: ldq $26,24($30)
// ASM-NEXT: ldq $15,0($30)
// ASM-NEXT: lda $30,32($30)
// ASM-NEXT: ret

int seh_code(void) {
  __try {
    may_fault();
  } __except (1) {
    return __exception_code();
  }
  return 0;
}

// The exception code arrives in v0, which is also the return register.
// ASM-LABEL: seh_code:
// ASM: $ehgcr_3_4:
// ASM-NOT: $0
// ASM: br $31,.LBB3_6
// ASM: .long 1 # CatchAll

// The frame-escape offsets are assembler constants, not relocations.
// OBJ: Section {{.*}} .text {
// OBJ-NOT: frame_escape
// OBJ: Section {{.*}} .xdata {
// OBJ: IMAGE_REL_ALPHA_REFLONG ?filt$0@0@seh_filter@@
// OBJ: IMAGE_REL_ALPHA_REFLONG ?dtor$4@?0?seh_finally@4HA
// OBJ: Section {{.*}} .pdata {
// OBJ: IMAGE_REL_ALPHA_REFLONG __C_specific_handler
