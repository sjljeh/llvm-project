// RUN: %clang_cc1 -triple alpha-unknown-linux-gnu -E -dM %s -o - | FileCheck %s --check-prefixes=CHECK,LINUX
// RUN: %clang_cc1 -triple alpha-unknown-netbsd -E -dM %s -o - | FileCheck %s --check-prefixes=CHECK,NETBSD
// RUN: %clang_cc1 -triple alpha-unknown-openbsd -E -dM %s -o - | FileCheck %s --check-prefixes=CHECK,OPENBSD

// CHECK: #define _LP64 1
// CHECK: #define __BIGGEST_ALIGNMENT__ 16
// CHECK: #define __INT64_TYPE__
// CHECK: #define __INTPTR_TYPE__ long int
// CHECK: #define __LDBL_MANT_DIG__ 113
// CHECK: #define __LDBL_MAX_EXP__ 16384
// CHECK: #define __LONG_WIDTH__ 64
// CHECK: #define __LP64__ 1
// NETBSD: #define __NetBSD__ 1
// OPENBSD: #define __OpenBSD__ 1
// CHECK: #define __POINTER_WIDTH__ 64
// CHECK: #define __SIZEOF_INT128__ 16
// CHECK: #define __SIZEOF_LONG_DOUBLE__ 16
// CHECK: #define __SIZEOF_LONG__ 8
// CHECK: #define __SIZEOF_POINTER__ 8
// CHECK: #define __SIZE_TYPE__ long unsigned int
// CHECK: #define __alpha 1
// CHECK: #define __alpha__ 1
// LINUX: #define __linux 1
// LINUX: #define __linux__ 1
