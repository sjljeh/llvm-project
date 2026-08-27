// RUN: %clang_cc1 -triple alpha-pc-windows-msvc -target-feature +taso -E -dM %s -o - | FileCheck %s

// CHECK: #define _M_ALPHA 1
// CHECK: #define _WIN32 1
// CHECK-NOT: #define _WIN64
// CHECK: #define __ILP32__ 1
// CHECK: #define __LONG_WIDTH__ 32
// CHECK: #define __POINTER_WIDTH__ 32
// CHECK: #define __SIZEOF_LONG__ 4
// CHECK: #define __SIZEOF_POINTER__ 4
