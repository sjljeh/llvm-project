// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fms-compatibility -verify %s

unsigned _rotl(unsigned, int);

#pragma function(_disable, _rotl)
void _disable(void) {}
unsigned _rotl(unsigned value, int shift) { return value; }

void _enable(void) {} // expected-error {{definition of builtin function '_enable'}}
