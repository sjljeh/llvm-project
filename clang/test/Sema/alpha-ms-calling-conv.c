// RUN: %clang_cc1 -triple alpha-pc-windows-msvc -fsyntax-only -verify=windows %s
// RUN: %clang_cc1 -triple alpha-pc-windows-msvc -target-feature +taso -fsyntax-only -verify=windows %s
// RUN: %clang_cc1 -triple alpha-unknown-linux-gnu -fsyntax-only -verify=elf %s

// windows-no-diagnostics

void __attribute__((stdcall)) stdcall_function(void); // elf-warning {{calling convention is not supported for this target}}
void __attribute__((fastcall)) fastcall_function(void); // elf-warning {{calling convention is not supported for this target}}
void __attribute__((thiscall)) thiscall_function(void); // elf-warning {{calling convention is not supported for this target}}