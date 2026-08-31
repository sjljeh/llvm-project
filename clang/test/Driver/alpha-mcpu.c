// RUN: %clang -target alpha-pc-windows-msvc -mcpu=ev4 -### -c %s 2>&1 | FileCheck %s --check-prefix=EV4
// RUN: %clang -target alpha-pc-windows-msvc -mcpu=ev7 -### -c %s 2>&1 | FileCheck %s --check-prefix=EV7

// EV4: "-target-cpu" "ev4"
// EV7: "-target-cpu" "ev7"
