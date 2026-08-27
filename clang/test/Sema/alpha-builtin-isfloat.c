// RUN: %clang_cc1 -triple alpha-pc-windows-msvc -fms-extensions -DTEST_ALPHA -verify=alpha %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions -verify=x86 %s

#ifdef TEST_ALPHA
_Static_assert(__builtin_isfloat(float), "float must use Alpha's FP save area");
_Static_assert(__builtin_isfloat(double), "double must use Alpha's FP save area");
_Static_assert(!__builtin_isfloat(int), "integer must use Alpha's integer save area");
// alpha-no-diagnostics
#else
int unsupported = __builtin_isfloat(float); // x86-error {{builtin is not supported on this target}}
#endif
