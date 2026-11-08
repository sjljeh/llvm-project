// Windows MIPS and Alpha probe the stack inline by default (NT grows a stack
// only through its guard page); -fno-stack-clash-protection and
// -mno-stack-arg-probe turn probing off.

// RUN: %clang --target=mipsel-pc-windows-msvc -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=alpha-pc-windows-msvc -mtaso -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=mipsel-pc-windows-msvc -fno-stack-clash-protection -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang --target=alpha-pc-windows-msvc -mtaso -fno-stack-clash-protection -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang --target=mipsel-pc-windows-msvc -mno-stack-arg-probe -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang --target=alpha-pc-windows-msvc -mtaso -mno-stack-arg-probe -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang --target=mipsel-pc-windows-msvc -fno-stack-clash-protection -fstack-clash-protection -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=alpha-pc-windows-msvc -mtaso -mno-stack-arg-probe -mstack-arg-probe -### -c %s 2>&1 | FileCheck %s --check-prefix=ON
// RUN: %clang --target=mipsel-unknown-linux-gnu -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang --target=mips-pc-windows-msvc -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF

// ON:  "-fstack-clash-protection"
// OFF-NOT: "-fstack-clash-protection"

// The backends key on the function attributes.
// RUN: %if mips-registered-target %{ %clang --target=mipsel-pc-windows-msvc -S -emit-llvm %s -o - | FileCheck %s --check-prefix=IR %}
// RUN: %if alpha-registered-target %{ %clang --target=alpha-pc-windows-msvc -mtaso -mstack-probe-size=8192 -S -emit-llvm %s -o - | FileCheck %s --check-prefixes=IR,SIZE %}
// RUN: %if mips-registered-target %{ %clang --target=mipsel-pc-windows-msvc -fno-stack-clash-protection -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-IR %}

// IR: "probe-stack"="inline-asm"
// SIZE-SAME: "stack-probe-size"="8192"
// NO-IR-NOT: "probe-stack"

void f(void) {}
