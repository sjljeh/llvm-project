// RUN: %clang_cc1 -triple riscv32-pc-windows-msvc -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple riscv64-pc-windows-msvc -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple riscv64-w64-windows-gnu -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple powerpcle-pc-windows-msvc -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple powerpcle-w64-windows-gnu -fsyntax-only -verify %s
// RUN: %clang_cc1 -triple riscv64-unknown-linux-gnu -fsyntax-only -verify=linux %s

// expected-no-diagnostics

void __attribute__((stdcall)) f1(void); // linux-warning {{'stdcall' calling convention is not supported for this target}}
void __attribute__((fastcall)) f2(void); // linux-warning {{'fastcall' calling convention is not supported for this target}}
void __attribute__((thiscall)) f3(void *); // linux-warning {{'thiscall' calling convention is not supported for this target}}
void __attribute__((vectorcall)) f4(void); // linux-warning {{'vectorcall' calling convention is not supported for this target}}
