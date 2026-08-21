// RUN: %clang_cc1 %s -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -Wno-jump-seh-finally -emit-llvm -O1 -disable-llvm-passes -o - \
// RUN:   | opt -S -passes=verify | FileCheck %s
// RUN: %clang_cc1 %s -triple i686-pc-windows-msvc -fms-extensions \
// RUN:   -Wno-jump-seh-finally -emit-llvm -O1 -disable-llvm-passes -o - \
// RUN:   | opt -passes=verify -disable-output

void use(int);

int break_from_finally(int value) {
  while (value > 0) {
    __try {
      --value;
    } __finally {
      if (value == 2)
        break;
    }
  }
  return value;
}

// CHECK-LABEL: define dso_local i32 @break_from_finally(
// CHECK: call void @"?fin$0@0@break_from_finally@@"(
// CHECK: %[[DEST:.+]] = load i32, ptr %{{.+}}
// CHECK: switch i32 %[[DEST]], label %{{.+}} [
// CHECK-NEXT: i32 1, label %[[BREAK:.+]]
// CHECK: [[BREAK]]:
// CHECK: br label %[[EXIT:.+]]
// CHECK: [[EXIT]]:
// CHECK: ret i32

// CHECK-LABEL: define internal void @"?fin$0@0@break_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: ret void

int continue_from_finally(int value) {
  for (int index = 0; index != 4; ++index) {
    __try {
      use(index);
    } __finally {
      if (index == value)
        continue;
    }
    use(10);
  }
  return value;
}

// CHECK-LABEL: define dso_local i32 @continue_from_finally(
// CHECK: call void @"?fin$0@0@continue_from_finally@@"(
// CHECK: switch i32 %{{.+}}, label %{{.+}} [
// CHECK-NEXT: i32 1, label %[[CONTINUE:.+]]
// CHECK: [[CONTINUE]]:
// CHECK: br label %[[INC:.+]]
// CHECK: [[INC]]:
// CHECK: add nsw i32

// CHECK-LABEL: define internal void @"?fin$0@0@continue_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: ret void

int goto_from_finally(int value) {
  __try {
    use(value);
  } __finally {
    if (value)
      goto done;
  }
  value = 0;
done:
  return value;
}

// CHECK-LABEL: define dso_local i32 @goto_from_finally(
// CHECK: call void @"?fin$0@0@goto_from_finally@@"(
// CHECK: switch i32 %{{.+}}, label %{{.+}} [
// CHECK-NEXT: i32 1, label %[[GOTO:.+]]
// CHECK: [[GOTO]]:
// CHECK: br label %[[DONE:.+]]
// CHECK: [[DONE]]:
// CHECK: ret i32

// CHECK-LABEL: define internal void @"?fin$0@0@goto_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: ret void

void internal_jumps(int value) {
  __try {
    use(value);
  } __finally {
  again:
    while (value) {
      if (--value)
        continue;
      break;
    }
    if (value)
      goto again;
  }
}

// Internal jumps remain entirely in the outlined helper and need no parent
// bailout slot.
// CHECK-LABEL: define dso_local void @internal_jumps(
// CHECK-NOT: switch i32
// CHECK: ret void
// CHECK-LABEL: define internal void @"?fin$0@0@internal_jumps@@"(
// CHECK-NOT: store i32 1
// CHECK: ret void

void nested_break_from_finally(int value) {
  while (value) {
    __try {
      use(value);
    } __finally {
      __try {
        use(value);
      } __finally {
        if (value)
          break;
      }
    }
    use(0);
  }
}

// The inner helper propagates the same statement through the outer helper.
// CHECK-LABEL: define internal void @"?fin$0@0@nested_break_from_finally@@"(
// CHECK: call void @"?fin$1@0@nested_break_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: ret void
// CHECK-LABEL: define internal void @"?fin$1@0@nested_break_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: ret void

int return_from_finally(int value) {
  __try {
    use(value);
  } __finally {
    if (value)
      return 7;
  }
  return 3;
}

// CHECK-LABEL: define dso_local i32 @return_from_finally(
// CHECK: call void @"?fin$0@0@return_from_finally@@"(
// CHECK: switch i32 %{{.+}}, label %{{.+}} [
// CHECK-NEXT: i32 1, label %[[RETURN:.+]]
// CHECK: [[RETURN]]:
// CHECK: br label %[[RETURN_BLOCK:.+]]
// CHECK: [[RETURN_BLOCK]]:
// CHECK: load i32, ptr %{{.+}}
// CHECK-NEXT: ret i32

// CHECK-LABEL: define internal void @"?fin$0@0@return_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: store i32 7, ptr %{{.+}}
// CHECK: ret void

int nested_return_from_finally(int value) {
  __try {
    use(value);
  } __finally {
    __try {
      use(value);
    } __finally {
      if (value)
        return 11;
    }
  }
  return 5;
}

// The return value and destination propagate through both helpers.
// CHECK-LABEL: define internal void @"?fin$0@0@nested_return_from_finally@@"(
// CHECK: call void @"?fin$1@0@nested_return_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: ret void
// CHECK-LABEL: define internal void @"?fin$1@0@nested_return_from_finally@@"(
// CHECK: store i32 1, ptr %{{.+}}
// CHECK: store i32 11, ptr %{{.+}}
// CHECK: ret void
