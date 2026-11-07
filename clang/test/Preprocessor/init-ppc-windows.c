// Copyright (c) 2026 Ahmed ARIF

// RUN: %clang_cc1 -E -dM -ffreestanding -triple=powerpcle-pc-windows-msvc < /dev/null | FileCheck -match-full-lines %s --check-prefix=CHECK
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=powerpcle-w64-windows-gnu < /dev/null | FileCheck -match-full-lines %s --check-prefixes=CHECK,MINGW
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=powerpcle-pc-windows-msvc -target-cpu 604 < /dev/null | FileCheck -match-full-lines %s --check-prefix=CPU604

// CHECK-NOT: #define _LP64 1
// CHECK: #define _M_PPC 601
// CHECK: #define _WIN32 1
// CHECK-NOT: #define _WIN64 1
// CHECK: #define __INTPTR_TYPE__ int
// CHECK: #define __LITTLE_ENDIAN__ 1
// CHECK: #define __LONG_MAX__ 2147483647L
// MINGW: #define __MINGW32__ 1
// CHECK: #define __POINTER_WIDTH__ 32
// CHECK: #define __PTRDIFF_TYPE__ int
// CHECK: #define __SIZEOF_LONG_DOUBLE__ 8
// CHECK: #define __SIZEOF_LONG__ 4
// CHECK: #define __SIZEOF_POINTER__ 4
// CHECK: #define __SIZEOF_SIZE_T__ 4
// CHECK: #define __SIZE_TYPE__ unsigned int
// CHECK: #define __WCHAR_TYPE__ unsigned short
// CHECK: #define __WINT_TYPE__ unsigned short
// CHECK: #define __powerpc__ 1

// CPU604: #define _M_PPC 604
