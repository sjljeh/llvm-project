// RUN: %clang_cc1 -triple x86_64-windows-msvc -fms-extensions -verify %s
// RUN: %clang_cc1 -triple aarch64-windows-msvc -fms-extensions -verify %s

// expected-no-diagnostics

typedef char jmp_buf[1];

// Including setjmp.h before setjmpex.h leaves _setjmpex undeclared because
// setjmp.h is guarded when setjmpex.h changes this macro.
#define setjmp _setjmpex

int test_setjmpex(jmp_buf buffer) {
  return setjmp(buffer);
}
