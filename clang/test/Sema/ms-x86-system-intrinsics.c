// RUN: %clang_cc1 -triple i686-pc-windows-msvc -ffreestanding \
// RUN:   -fms-extensions -fms-compatibility -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -ffreestanding \
// RUN:   -fms-extensions -fms-compatibility -fsyntax-only -verify %s

typedef __SIZE_TYPE__ size_t;
#include <intrin.h>

void test_control_register_type(void) { (void)__readcr0(); }

void test_debug_registers(unsigned reg, size_t value) {
  (void)__readdr(0);
  (void)__readdr(1);
  (void)__readdr(2);
  (void)__readdr(3);
  (void)__readdr(4);
  (void)__readdr(5);
  (void)__readdr(6);
  (void)__readdr(7);

  __writedr(0, value);
  __writedr(4, value);
  __writedr(5, value);
  __writedr(7, value);

  (void)__readdr(reg); // expected-error {{must be a constant integer}}
  __writedr(reg, value); // expected-error {{must be a constant integer}}
  (void)__readdr(8);      // expected-error {{must be between 0 and 7}}
  __writedr(8, value);    // expected-error {{must be between 0 and 7}}
}
