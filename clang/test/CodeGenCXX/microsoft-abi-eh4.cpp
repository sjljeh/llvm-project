// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fcxx-exceptions -fexceptions -fms-cxx-eh4 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=FH4
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fcxx-exceptions -fexceptions -fms-cxx-eh4 -fno-ms-cxx-eh4 \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=FH3
// RUN: %clang_cc1 -triple i686-pc-windows-msvc -fms-extensions \
// RUN:   -fcxx-exceptions -fexceptions -fms-cxx-eh4 -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=FH3
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fcxx-exceptions -fexceptions -fms-cxx-eh4 -O2 -S -o - %s \
// RUN:   | FileCheck %s --check-prefix=ASM

extern void may_throw();
extern int may_throw_value(int);
extern int cannot_throw_value(int) noexcept;

void use_exception() {
  try {
    may_throw();
  } catch (...) {
  }
}

void pure_noexcept() noexcept { may_throw(); }
void empty_noexcept() noexcept {}
__declspec(nothrow) void declspec_nothrow() { may_throw(); }

static void optimized_away_callee() {}
void optimized_noexcept() noexcept { optimized_away_callee(); }

int noexcept_to_throwing(int x) noexcept { return may_throw_value(x); }
int noexcept_to_nothrow(int x) noexcept { return cannot_throw_value(x); }
int mixed_noexcept(int x) noexcept {
  if (x)
    may_throw();
  return cannot_throw_value(x);
}
int musttail_noexcept(int x) noexcept {
  [[clang::musttail]] return cannot_throw_value(x);
}

// FH4: personality ptr @__CxxFrameHandler4
// FH4: declare{{.*}} i32 @__CxxFrameHandler4(...)
// FH4-LABEL: define{{.*}} void @"?pure_noexcept@@YAXXZ"
// FH4-SAME: () #[[NOEXCEPT:[0-9]+]] personality ptr @__CxxFrameHandler4
// FH4: notail call void @"?may_throw@@YAXXZ"()
// FH4-NOT: __std_terminate
// FH4-LABEL: define{{.*}} void @"?empty_noexcept@@YAXXZ"()
// FH4-NOT: personality
// FH4-NEXT: entry:
// FH4-LABEL: define{{.*}} void @"?declspec_nothrow@@YAXXZ"
// FH4: invoke void @"?may_throw@@YAXXZ"()
// FH4: unwind label %terminate
// FH4: terminate:
// FH4: call void @{{.*terminate.*}}()
// FH4-LABEL: define{{.*}} i32 @"?musttail_noexcept@@YAHH@Z"
// FH4: musttail call{{.*}} @"?cannot_throw_value@@YAHH@Z"
// FH4-NEXT: ret i32
// FH4: attributes #[[NOEXCEPT]] = { {{.*}}nounwind{{.*}}"msvc-cxx-eh4-noexcept"
// FH3: personality ptr @__CxxFrameHandler3
// FH3: declare{{.*}} i32 @__CxxFrameHandler3(...)

// __declspec(nothrow) retains an explicit terminate funclet and does not use
// FH4's NoExcept bit.
// ASM-LABEL: "?declspec_nothrow@@YAXXZ":
// ASM: "$cppxdata$?declspec_nothrow@@YAXXZ":
// ASM-NEXT: .byte 40
// A call eliminated by optimization must not leave a handler on an otherwise
// empty noexcept function.
// ASM-LABEL: "?optimized_noexcept@@YAXXZ":
// ASM-NOT: .seh_handler
// ASM: retq
// A throwing callee needs this function's FH4 frame, whereas a noexcept callee
// can enforce its own contract after a tail call.
// ASM-LABEL: "?noexcept_to_throwing@@YAHH@Z":
// ASM: .seh_handler __CxxFrameHandler4, @unwind, @except
// ASM: callq "?may_throw_value@@YAHH@Z"
// ASM: "$cppxdata$?noexcept_to_throwing@@YAHH@Z":
// ASM-NEXT: .byte 96
// ASM-LABEL: "?noexcept_to_nothrow@@YAHH@Z":
// ASM-NOT: .seh_handler
// ASM: jmp "?cannot_throw_value@@YAHH@Z"{{.*}}# TAILCALL
// ASM-LABEL: "?mixed_noexcept@@YAHH@Z":
// ASM: .seh_handler __CxxFrameHandler4, @unwind, @except
// ASM: callq "?may_throw@@YAXXZ"
// ASM: jmp "?cannot_throw_value@@YAHH@Z"{{.*}}# TAILCALL
