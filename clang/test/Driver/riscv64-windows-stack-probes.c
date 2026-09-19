// REQUIRES: riscv-registered-target
// RUN: %clang --target=riscv64-w64-windows-gnu -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=riscv64-pc-windows-msvc -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -fstack-clash-protection -Werror -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -mno-stack-arg-probe -mstack-arg-probe -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -fno-stack-clash-protection -fstack-clash-protection -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -mno-stack-arg-probe -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -mstack-arg-probe -mno-stack-arg-probe -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -fstack-clash-protection -fno-stack-clash-protection -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -mstack-probe-size=2048 -S -emit-llvm %s -o - | FileCheck %s --check-prefixes=PROBE,SIZE
// RUN: %clang --target=riscv64-unknown-linux-gnu -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// RUN: %clang --target=riscv64-w64-windows-gnu -march=rv64gc -mabi=lp64 -O2 -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang --target=riscv64-w64-windows-gnu -march=rv64gc -mabi=lp64 -O2 -c %s -o %t.obj

// PROBE: "probe-stack"="inline-asm"
// SIZE-SAME: "stack-probe-size"="2048"
// NO-PROBE-NOT: "probe-stack"

extern void use(void *);

// ASM-LABEL: fixed:
// ASM: lui [[PAGE:[a-z0-9]+]], 1
// ASM-NEXT: sub sp, sp, [[PAGE]]
// ASM: sd zero, 0(sp)
// ASM: call use
void fixed(void) {
  char buffer[65536];
  use(buffer);
}

// ASM-LABEL: dynamic:
// ASM: sub sp, sp,
// ASM: sd zero, 0(sp)
// ASM: call use
void dynamic(unsigned long long size) {
  use(__builtin_alloca(size));
}
