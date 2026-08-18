// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fms-compatibility -verify %s

#pragma function(_disable)
void _disable(void) {} // expected-error {{definition of builtin function '_disable'}}
