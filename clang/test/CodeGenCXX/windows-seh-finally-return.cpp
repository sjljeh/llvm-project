// RUN: %clang_cc1 %s -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -Wno-jump-seh-finally -emit-llvm -O1 -disable-llvm-passes -o - \
// RUN:   | opt -S -passes=verify | FileCheck %s
// RUN: %clang_cc1 %s -triple i686-pc-windows-msvc -fms-extensions \
// RUN:   -Wno-jump-seh-finally -emit-llvm -O1 -disable-llvm-passes -o - \
// RUN:   | opt -passes=verify -disable-output

struct Pair {
  int first;
  int second;
};

Pair return_pair(int value) {
  __try {
    ++value;
  } __finally {
    if (value == 2)
      return {7, 8};
  }
  return {3, 4};
}

// CHECK-LABEL: define dso_local i64 @"?return_pair@@YA?AUPair@@H@Z"(
// CHECK: call void @"?fin$0@0@return_pair@@"(
// CHECK: switch i32 %{{.+}}, label %{{.+}} [
// CHECK-NEXT: i32 1, label %[[RETURN:.+]]
// CHECK: [[RETURN]]:
// CHECK: br label %[[RETURN_BLOCK:.+]]
// CHECK: [[RETURN_BLOCK]]:
// CHECK: load i64, ptr %{{.+}}
// CHECK-NEXT: ret i64

// CHECK-LABEL: define internal void @"?fin$0@0@return_pair@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: getelementptr inbounds nuw %struct.Pair, ptr %{{.+}}, i32 0, i32 0
// CHECK: store i32 7, ptr %{{.+}}
// CHECK: getelementptr inbounds nuw %struct.Pair, ptr %{{.+}}, i32 0, i32 1
// CHECK: store i32 8, ptr %{{.+}}
// CHECK: ret void
