// REQUIRES: riscv-registered-target
// RUN: %clang --target=riscv64-w64-windows-gnu -march=rv64gc -mabi=lp64 -mno-relax -ffreestanding -funwind-tables -fms-extensions -Xclang -fasync-exceptions -O0 -S %s -o - | FileCheck %s
// RUN: %clang --target=riscv64-w64-windows-gnu -march=rv64gc -mabi=lp64 -mno-relax -ffreestanding -funwind-tables -fms-extensions -Xclang -fasync-exceptions -O2 -S %s -o - | FileCheck %s
// RUN: %clang --target=riscv64-w64-windows-gnu -march=rv64gc -mabi=lp64 -mno-relax -ffreestanding -funwind-tables -fms-extensions -Xclang -fasync-exceptions -O0 -c %s -o %t.o
// RUN: %clang --target=riscv64-w64-windows-gnu -march=rv64gc -mabi=lp64 -mno-relax -ffreestanding -funwind-tables -fms-extensions -Xclang -fasync-exceptions -O2 -c %s -o %t.o
// RUN: %clang_cl --target=riscv64-pc-windows-msvc /kernel /Od /c /Fo%t.kernel.obj %s
// RUN: llvm-readobj --file-headers --unwind %t.kernel.obj | FileCheck %s --check-prefix=KERNEL

// KERNEL: Machine: IMAGE_FILE_MACHINE_RISCV64
// KERNEL: RuntimeFunction {
// KERNEL: Opcode: SET_CFA
// KERNEL: ExceptionHandler: __C_specific_handler

extern void work(void *);
extern int filter(void *, int);
extern void *_exception_info(void);

// CHECK-LABEL: captured:
// CHECK: .seh_handler __C_specific_handler, @unwind, @except
// CHECK: .Lcaptured$frame_escape_0 = -{{[0-9]+}}
int captured(int input) {
  volatile int local = input + 7;
  __try { work((void *)&local); }
  __except (filter(_exception_info(), local)) { return local + 1; }
  return local;
}

// CHECK-LABEL: cleanup:
// CHECK: .seh_handler __C_specific_handler, @unwind, @except
// CHECK: .seh_proc "?dtor${{[0-9]+}}@?0?cleanup@4HA"
// CHECK: .seh_set_cfa x2, {{[0-9]+}}
// CHECK: .seh_save_gpr x8,
// CHECK: mv s0, a1
// CHECK-NOT: .seh_set_cfa x8
// CHECK: .seh_endprologue
// CHECK: .seh_startepilogue
// CHECK: ld s0,
// CHECK: .seh_same_gpr x8
// CHECK: ret
// CHECK-NEXT: .seh_endepilogue
void cleanup(int input) {
  volatile int local = input;
  __try { work((void *)&local); }
  __finally { work((void *)&local); }
}

// Large fixed frames need both SP adjustments described in a funclet, with
// no duplicate CFA transition at the split epilogue adjustment.
// CHECK-LABEL: large:
// CHECK: .seh_proc "?dtor${{[0-9]+}}@?0?large@4HA"
// CHECK: mv s0, a1
// CHECK-NOT: .seh_set_cfa x8
// CHECK: .seh_set_cfa x2, {{[0-9]+}}
// CHECK: .seh_endprologue
// CHECK: .seh_startepilogue
// CHECK: .seh_set_cfa x2,
// CHECK-NOT: .seh_set_cfa
// CHECK: ld ra,
// CHECK: ret
void large(void) {
  volatile char local[8192];
  local[0] = 1;
  __try { work((void *)local); }
  __finally { work((void *)local); }
}

// Compilation also covers multiple outlined funclets sharing parent locals.
void nested(int input) {
  volatile int local = input;
  __try {
    __try { work((void *)&local); }
    __finally { work((void *)&local); }
  } __finally { work((void *)&local); }
}
