// RUN: %clang_cc1 -triple powerpcle-pc-windows-msvc -fms-extensions -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple powerpcle-pc-windows-msvc -fms-extensions -emit-obj -o %t %s
// RUN: llvm-objdump -d %t | FileCheck %s --check-prefix=ASM
// RUN: %clang_cc1 -triple powerpcle-pc-windows-msvc -fms-extensions -DERROR -fsyntax-only -verify %s

void __emit(unsigned const __int32);
struct TEB;
struct TEB *__builtin_get_gpr13(void);

void barriers(void) {
  __emit(0x7C0006AC);
  __emit(0x7C0004AC);
  __emit(0x4C00012C);
}
// CHECK-LABEL: define dso_local void @barriers()
// CHECK: call void asm sideeffect ".long 0x7C0006AC", ""()
// CHECK: call void asm sideeffect ".long 0x7C0004AC", ""()
// CHECK: call void asm sideeffect ".long 0x4C00012C", ""()

struct TEB *current_teb(void) {
  return __builtin_get_gpr13();
}
// CHECK-LABEL: define dso_local ptr @current_teb()
// CHECK: call i32 @llvm.read_register.i32(metadata ![[REG:[0-9]+]])
// CHECK: inttoptr i32 {{.*}} to ptr
// CHECK: ![[REG]] = !{!"r13"}

// ASM-LABEL: <.text>:
// ASM: eieio
// ASM: sync
// ASM: isync
// ASM-LABEL: <..current_teb>:
// ASM: mr 3, 13

#ifdef ERROR
void bad_emit(unsigned opcode) {
  __emit(opcode); // expected-error {{argument to '__emit' must be a constant integer}}
}
#endif
