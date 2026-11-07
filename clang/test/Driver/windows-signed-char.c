// RUN: %clang -### --target=riscv64-w64-windows-gnu -c %s 2>&1 | FileCheck %s --check-prefix=SIGNED
// RUN: %clang -### --target=riscv64-pc-windows-msvc -c %s 2>&1 | FileCheck %s --check-prefix=SIGNED
// RUN: %clang -### --target=powerpcle-w64-windows-gnu -c %s 2>&1 | FileCheck %s --check-prefix=SIGNED
// RUN: %clang -### --target=riscv64-unknown-linux-gnu -c %s 2>&1 | FileCheck %s --check-prefix=UNSIGNED
// RUN: %clang -### --target=powerpc64le-unknown-linux-gnu -c %s 2>&1 | FileCheck %s --check-prefix=UNSIGNED

// SIGNED-NOT: "-fno-signed-char"
// UNSIGNED: "-fno-signed-char"
