// RUN: %clang_cc1 -triple riscv64-w64-windows-gnu -fms-extensions -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions -fasync-exceptions -emit-llvm %s -o - | FileCheck %s

int report(void);
void clean(void);

// CHECK-LABEL: define {{.*}}void @phi_only_try(
// CHECK: call void @{{.*}}fin
// CHECK: ret void
void phi_only_try(int condition) {
  __try {
    // The last block contains only the conditional expression's PHI when
    // VolatilizeTryBlocks runs, before the SEH epilogue is emitted.
    (void)(condition ? 1 : (report(), 0));
  } __finally {
    clean();
  }
}
