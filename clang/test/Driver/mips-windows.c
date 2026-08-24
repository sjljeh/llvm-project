// RUN: %clang --target=mipsel-pc-windows-msvc -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=mipsel-pc-windows-msvc -### -c %s -mcpu=r4000 2>&1 \
// RUN:   | FileCheck %s --check-prefix=R4000
// RUN: %clang_cl --target=mipsel-pc-windows-msvc /QMR4600 /c -### -- %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=R4600
// RUN: %clang_cl --target=mipsel-pc-windows-msvc /QMR10000 /clang:-dM /E -- \
// RUN:   %s 2>&1 | FileCheck %s --check-prefix=R10000-MACRO

// DEFAULT: "-funwind-tables=2"
// DEFAULT: "-target-cpu" "mips2"
// R4000: "-target-cpu" "r4000"
// R4600: "-target-cpu" "r4600"
// R10000-MACRO: #define _M_MRX000 10000
