// Copyright (c) 2026 Ahmed ARIF

// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple i686-pc-windows-msvc -fms-extensions -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-pc-windows-msvc -fms-extensions -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple thumbv7-pc-windows-msvc -fms-extensions -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple powerpcle-pc-windows-msvc -fms-extensions -emit-llvm %s -o - | FileCheck %s

extern void consume(int);

// A goto through two finally blocks is abnormal for both. The inner cleanup
// branches straight to the outer one without an exit switch, but must still
// use the nonzero cleanup destination to report abnormal termination.
// CHECK-LABEL: define dso_local {{.*}}void @nested_goto(
// CHECK: store {{(volatile )?}}i32 [[DEST:[1-9][0-9]*]], ptr %cleanup.dest.slot
// CHECK: [[INNER_DEST:%.*]] = load {{(volatile )?}}i32, ptr %cleanup.dest.slot
// CHECK-NEXT: [[INNER_ABNORMAL:%.*]] = icmp ne i32 [[INNER_DEST]], 0
// CHECK-NEXT: [[INNER_FLAG:%.*]] = zext i1 [[INNER_ABNORMAL]] to i8
// CHECK-NEXT: {{(call|invoke).*}}void @"?fin$1@0@nested_goto@@"(i8 noundef{{( zeroext)?}} [[INNER_FLAG]],
// CHECK: [[OUTER_DEST:%.*]] = load {{(volatile )?}}i32, ptr %cleanup.dest.slot
// CHECK-NEXT: [[OUTER_ABNORMAL:%.*]] = icmp ne i32 [[OUTER_DEST]], 0
// CHECK-NEXT: [[OUTER_FLAG:%.*]] = zext i1 [[OUTER_ABNORMAL]] to i8
// CHECK-NEXT: {{(call|invoke).*}}void @"?fin$0@0@nested_goto@@"(i8 noundef{{( zeroext)?}} [[OUTER_FLAG]],
void nested_goto(void) {
  __try {
    __try {
      goto end;
    } __finally {
      consume(__abnormal_termination());
    }
  } __finally {
    consume(__abnormal_termination());
  }
end:;
}

// __leave only exits the inner try normally. Falling out of the outer try is
// normal as well, so neither finally block should load a destination slot.
// CHECK-LABEL: define dso_local {{.*}}void @nested_leave(
// CHECK-NOT: %cleanup.dest.slot
// CHECK: {{(call|invoke).*}}void @"?fin$1@0@nested_leave@@"(i8 noundef{{( zeroext)?}} 0,
// CHECK-NOT: %cleanup.dest.slot
// CHECK: {{(call|invoke).*}}void @"?fin$0@0@nested_leave@@"(i8 noundef{{( zeroext)?}} 0,
void nested_leave(void) {
  __try {
    __try {
      __leave;
    } __finally {
      consume(__abnormal_termination());
    }
  } __finally {
    consume(__abnormal_termination());
  }
}

// A goto to a label still inside the outer try only terminates the inner try
// abnormally. Its destination must not leak into the outer fallthrough.
// CHECK-LABEL: define dso_local {{.*}}void @goto_inside_outer(
// CHECK: [[DEST:%.*]] = load {{(volatile )?}}i32, ptr %cleanup.dest.slot
// CHECK-NEXT: [[ABNORMAL:%.*]] = icmp ne i32 [[DEST]], 0
// CHECK-NEXT: [[FLAG:%.*]] = zext i1 [[ABNORMAL]] to i8
// CHECK-NEXT: {{(call|invoke).*}}void @"?fin$1@0@goto_inside_outer@@"(i8 noundef{{( zeroext)?}} [[FLAG]],
// CHECK: {{(call|invoke).*}}void @"?fin$0@0@goto_inside_outer@@"(i8 noundef{{( zeroext)?}} 0,
void goto_inside_outer(void) {
  __try {
    __try {
      goto inside;
    } __finally {
      consume(__abnormal_termination());
    }
  inside:;
  } __finally {
    consume(__abnormal_termination());
  }
}
