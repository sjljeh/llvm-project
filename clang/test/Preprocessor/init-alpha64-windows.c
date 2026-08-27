// RUN: %clang_cc1 -triple alpha-pc-windows-msvc -fms-compatibility -E -dM -x c /dev/null | FileCheck %s

// CHECK: #define _M_ALPHA 1
// CHECK: #define _WIN32 1
// CHECK: #define _WIN64 1
// CHECK: #define __SIZEOF_POINTER__ 8
// CHECK: #define __alpha 1
// CHECK: #define __alpha__ 1
// CHECK-NOT: #define _ILP32
