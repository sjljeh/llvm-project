// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fms-compatibility -emit-llvm -o - %s | FileCheck %s

void __cdecl _disable(void);

void intrinsic_before_pragma(void) {
  _disable();
}

// CHECK-LABEL: define dso_local void @intrinsic_before_pragma()
// CHECK: call void asm sideeffect "cli"

#pragma function(_disable)

void function_after_declaration(void) {
  _disable();
}

// CHECK-LABEL: define dso_local void @function_after_declaration()
// CHECK: call void @_disable()

#pragma function(_enable)
void __cdecl _enable(void);

void function_after_pragma(void) {
  _enable();
}

// CHECK-LABEL: define dso_local void @function_after_pragma()
// CHECK: call void @_enable()
