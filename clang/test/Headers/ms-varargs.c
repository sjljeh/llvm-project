// RUN: %clang -cc1 -triple x86_64-pc-windows-msvc -fms-compatibility \
// RUN:   -fms-compatibility-version=19.00 -fsyntax-only \
// RUN:   -isystem %S/../../lib/Headers \
// RUN:   -isystem %S/Inputs/ms-varargs %s
// RUN: not %clang -cc1 -triple x86_64-unknown-linux-gnu -fsyntax-only \
// RUN:   -isystem %S/../../lib/Headers \
// RUN:   -isystem %S/Inputs/ms-varargs %s 2>&1 \
// RUN:   | FileCheck %s

#include <varargs.h>

// CHECK: error: "Please use <stdarg.h> instead of <varargs.h>"

#ifndef va_dcl
#error va_dcl was not provided by the MSVC varargs header
#endif

#ifndef va_start
#error va_start was not provided by the MSVC varargs header
#endif
