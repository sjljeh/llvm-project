// Copyright (c) 2026 Ahmed ARIF

// REQUIRES: powerpc-registered-target

// MinGW links through the ppcpe emulation and uses native NT SEH tables.
// RUN: %clang --target=powerpcle-w64-windows-gnu -### -fuse-ld=lld %s 2>&1 | FileCheck %s --check-prefixes=COMMON,MINGW
// RUN: %clang --target=powerpcle-pc-windows-msvc -### -c %s 2>&1 | FileCheck %s --check-prefix=COMMON

// COMMON: "-cc1"
// COMMON-SAME: "-mrelocation-model" "static"
// COMMON-SAME: "-funwind-tables=2"
// COMMON-SAME: "-fstack-clash-protection"
// MINGW-SAME: "-exception-model=seh"
// MINGW: "-m" "ppcpe"

// Stack probes follow the public opt-out flags.
// RUN: %clang --target=powerpcle-w64-windows-gnu -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=powerpcle-pc-windows-msvc -S -emit-llvm %s -o - | FileCheck %s --check-prefix=PROBE
// RUN: %clang --target=powerpcle-w64-windows-gnu -mno-stack-arg-probe -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// RUN: %clang --target=powerpcle-w64-windows-gnu -fno-stack-clash-protection -S -emit-llvm %s -o - | FileCheck %s --check-prefix=NO-PROBE
// PROBE: "probe-stack"="inline-asm"
// NO-PROBE-NOT: "probe-stack"

// MinGW keeps the NT calling convention but uses the Itanium C++ ABI; MSVC
// uses the Microsoft C++ ABI.
// RUN: %clang --target=powerpcle-w64-windows-gnu -x c++ -S -emit-llvm %s -o - | FileCheck %s --check-prefix=ITANIUM
// RUN: %clang --target=powerpcle-pc-windows-msvc -x c++ -S -emit-llvm %s -o - | FileCheck %s --check-prefix=MSABI
// ITANIUM: define dso_local void @_Z3bigv()
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
