// RUN: %clang --target=mipsel-pc-windows-msvc -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang --target=mipsel-pc-windows-msvc -### -c %s -mcpu=r4000 2>&1 \
// RUN:   | FileCheck %s --check-prefix=R4000

// DEFAULT: "-target-cpu" "mips2"
// R4000: "-target-cpu" "r4000"
