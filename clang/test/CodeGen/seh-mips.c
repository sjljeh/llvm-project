// REQUIRES: mips-registered-target
//
// RUN: %clang -target mipsel-pc-windows-msvc -fms-extensions -O0 -S \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target mipsel-pc-windows-msvc -fms-extensions -O2 -S \
// RUN:   -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang -target mipsel-pc-windows-msvc -fms-extensions -O2 -c \
// RUN:   -o %t.obj %s
// RUN: llvm-readobj --relocations %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: not %clang_cc1 -triple mipsel-pc-windows-gnu -fms-extensions \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=GNU

// C SEH on NT MIPS: __C_specific_handler's scope tables hold absolute
// addresses, and Windows enters a filter or termination handler with its
// argument in a0, the handler's own address in a1, and the establisher frame
// (the parent's incoming $sp) in a2 and v0. RtlUnwind enters an __except
// block with the exception code in v0.

extern void may_fault(void);
extern int inspect_exception(unsigned code, int selector);
extern volatile int sink;

// GNU: error: SEH '__try' is not supported on this target
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

// The filter models the runtime's three arguments and takes the frame from
// the third.
// IR-LABEL: define dso_local i32 @seh_filter(
// IR-SAME: personality ptr @__C_specific_handler
// IR-LABEL: define internal i32 @"?filt$0@0@seh_filter@@"(ptr noundef %0, ptr noundef %1, ptr noundef %2)
// IR: call ptr @llvm.eh.recoverfp(ptr @seh_filter, ptr %2)

// ASM-LABEL: seh_filter:
// ASM: .seh_handler __C_specific_handler, @unwind, @except
// ASM: addiu $sp, $sp, -40
// ASM-NEXT: sw $ra, 32($sp)
// ASM-NEXT: sw $fp, 28($sp)
// ASM-NEXT: move $fp, $sp
// ASM-NEXT: .seh_endprologue
// llvm.localaddress is the incoming $sp; escaped locals are relative to it.
// ASM: .Lseh_filter$frame_escape_0 = -20
// ASM: sw ${{[0-9]+}}, 20($fp)
// The call's delay slot is inside the scope: NT unwinds to ra - 4.
// ASM: jal may_fault
// ASM-NEXT: nop
// ASM-NEXT: .Ltmp{{[0-9]+}}: # EH_LABEL
// $fp is reloaded last, directly before the return.
// ASM: move $sp, $fp
// ASM-NEXT: lw $ra, 32($sp)
// ASM-NEXT: lw $fp, 28($sp)
// ASM-NEXT: jr $ra
// ASM-NEXT: addiu $sp, $sp, 40
// ASM: .Lseh_filter$parent_frame_offset = 0
// ASM: .long {{.*}} # LabelEnd
// ASM-NEXT: .long "?filt$0@0@seh_filter@@" # FilterFunction
// ASM-NEXT: .long .LBB0_4 # ExceptionHandler

// ASM-LABEL: "?filt$0@0@seh_filter@@":
// ASM: lui $1, %hi(.Lseh_filter$parent_frame_offset)
// ASM-NEXT: addiu $1, $1, %lo(.Lseh_filter$parent_frame_offset)
// ASM-NEXT: addu $1, $6, $1
// ASM-NEXT: lui $2, %hi(.Lseh_filter$frame_escape_0)
// ASM-NEXT: addiu $2, $2, %lo(.Lseh_filter$frame_escape_0)
// ASM-NEXT: addu $1, $1, $2
// ASM-NEXT: lw $5, 0($1)
// ASM: jal inspect_exception

void seh_finally(int n) {
  int local = n + 3;
  __try {
    may_fault();
  } __finally {
    sink = local + __abnormal_termination();
  }
}

// The termination funclet runs on its own frame and addresses its parent's
// through $fp, which the runtime's v0 supplies. Its return block does not
// take $sp from that $fp, and it releases its frame in the delay slot.
// ASM-LABEL: "?dtor$3@?0?seh_finally@4HA":
// ASM: addiu $sp, $sp, -32
// ASM-NEXT: sw $ra, 24($sp)
// ASM-NEXT: sw $fp, 20($sp)
// ASM-NEXT: sw $2, 28($sp)
// ASM-NEXT: move $fp, $2
// ASM-NEXT: .seh_endprologue
// ASM: addiu $2, $fp, 0
// ASM-NOT: move $sp, $fp
// ASM: lw $fp, 20($sp)
// ASM-NEXT: jr $ra
// ASM-NEXT: addiu $sp, $sp, 32

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
// ASM-NOT: $2
// ASM: j .LBB3_3
// ASM: .long 1 # CatchAll

// OBJ: Section {{.*}} .xdata {
// OBJ: IMAGE_REL_MIPS_REFWORD ?filt$0@0@seh_filter@@
// OBJ: IMAGE_REL_MIPS_REFWORD ?dtor$3@?0?seh_finally@4HA
// OBJ: Section {{.*}} .pdata {
// OBJ: IMAGE_REL_MIPS_REFWORD __C_specific_handler
