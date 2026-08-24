// RUN: %clang_cc1 -E -dM -ffreestanding -triple=mipsel-pc-windows-msvc \
// RUN:   < /dev/null | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=mipsel-pc-windows-msvc \
// RUN:   -target-cpu r3000 < /dev/null | FileCheck %s --check-prefix=R3000
// RUN: %clang_cc1 -E -dM -ffreestanding -triple=mipsel-pc-windows-msvc \
// RUN:   -target-cpu r10000 < /dev/null | FileCheck %s --check-prefix=R10000

// DEFAULT: #define _M_MRX000 4000
// R3000: #define _M_MRX000 3000
// R10000: #define _M_MRX000 10000
