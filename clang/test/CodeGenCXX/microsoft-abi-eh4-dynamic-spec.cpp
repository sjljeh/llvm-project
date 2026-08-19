// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -std=c++14 -fms-extensions \
// RUN:   -fcxx-exceptions -fexceptions -fms-cxx-eh4 -emit-llvm -o - %s \
// RUN:   | FileCheck %s

extern void may_throw();

void ordinary() {
  try {
    may_throw();
  } catch (int) {
  }
}

void typed() throw(int) {
  try {
    may_throw();
  } catch (int) {
  }
}

void empty() throw() {
  try {
    may_throw();
  } catch (int) {
  }
}

void any() throw(...) {
  try {
    may_throw();
  } catch (int) {
  }
}

// MSVC treats typed dynamic exception specifications as noexcept(false), so
// they do not require an ESTypeList or force an FH3 personality.
// CHECK-LABEL: define{{.*}} void @"?ordinary@@YAXXZ"()
// CHECK-SAME: personality ptr @__CxxFrameHandler4
// CHECK-LABEL: define{{.*}} void @"?typed@@YAXXZ"()
// CHECK-SAME: personality ptr @__CxxFrameHandler4
// CHECK-LABEL: define{{.*}} void @"?empty@@YAXXZ"()
// CHECK-SAME: personality ptr @__CxxFrameHandler4
// CHECK-LABEL: define{{.*}} void @"?any@@YAXXZ"()
// CHECK-SAME: personality ptr @__CxxFrameHandler4
