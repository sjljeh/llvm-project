// Copyright (c) 2026 Ahmed ARIF

// REQUIRES: powerpc-registered-target

// RUN: %clang --target=powerpcle-pc-windows-msvc -### -c %s 2>&1 | FileCheck %s --check-prefix=COMMON

// COMMON: "-cc1"
// COMMON-SAME: "-mrelocation-model" "static"
// COMMON-SAME: "-funwind-tables=2"
// COMMON-SAME: "-fstack-clash-protection"

// Stack probes follow the public opt-out flags.
// RUN: %clang --target=powerpcle-pc-windows-msvc -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=powerpcle-pc-windows-msvc -mno-stack-arg-probe -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// RUN: %clang --target=powerpcle-pc-windows-msvc -fno-stack-clash-protection -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// PROBE: "probe-stack"="inline-asm"
// NO-PROBE-NOT: "probe-stack"

// MSVC uses the Microsoft C++ ABI.
// RUN: %clang --target=powerpcle-pc-windows-msvc -x c++ -S -emit-llvm %s -o - | FileCheck %s --check-prefix=MSABI
// MSABI: define dso_local void @"?big@@YAXXZ"()

#ifdef __cplusplus
extern void use(void *);
#else
extern void use(void *);
#endif

void big(void) {
  char buf[20000];
  use(buf);
}
