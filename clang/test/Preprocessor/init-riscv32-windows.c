// RUN: %clang_cc1 -E -dM -ffreestanding -triple=riscv32-pc-windows-msvc < /dev/null | FileCheck -match-full-lines %s

// CHECK: #define _INTEGRAL_MAX_BITS 64
// CHECK-NOT: #define _LP64 1
// CHECK: #define _M_RISCV32 100
// CHECK: #define _WIN32 1
// CHECK-NOT: #define _WIN64 1
// CHECK: #define __INT64_C_SUFFIX__ LL
// CHECK: #define __INT64_TYPE__ long long int
// CHECK: #define __INTMAX_TYPE__ long long int
// CHECK: #define __INTPTR_TYPE__ int
// CHECK: #define __LONG_MAX__ 2147483647L
// CHECK-NOT: #define __LP64__ 1
// CHECK: #define __POINTER_WIDTH__ 32
// CHECK: #define __PTRDIFF_TYPE__ int
// CHECK: #define __SIZEOF_LONG_DOUBLE__ 8
// CHECK: #define __SIZEOF_LONG__ 4
// CHECK: #define __SIZEOF_POINTER__ 4
// CHECK: #define __SIZEOF_SIZE_T__ 4
// CHECK: #define __SIZE_TYPE__ unsigned int
// CHECK: #define __UINT64_C_SUFFIX__ ULL
// CHECK: #define __UINT64_TYPE__ long long unsigned int
// CHECK: #define __UINTMAX_TYPE__ long long unsigned int
// CHECK: #define __UINTPTR_TYPE__ unsigned int
// CHECK: #define __WCHAR_TYPE__ unsigned short
// CHECK: #define __WINT_TYPE__ unsigned short
// CHECK: #define __riscv 1
// CHECK: #define __riscv_xlen 32
