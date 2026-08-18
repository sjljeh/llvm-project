// RUN: %clang_cc1 -triple i686-pc-windows-msvc -ffreestanding \
// RUN:   -fms-extensions -fms-compatibility \
// RUN:   -fdefault-calling-conv=stdcall -emit-llvm -o - %s | \
// RUN:   FileCheck %s --check-prefixes=CHECK,X86
// RUN: %clang_cc1 -x c++ -triple i686-pc-windows-msvc -ffreestanding \
// RUN:   -fms-extensions -fms-compatibility \
// RUN:   -fdefault-calling-conv=stdcall -emit-llvm -o - %s | \
// RUN:   FileCheck %s --check-prefixes=CHECK,X86
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -ffreestanding \
// RUN:   -fms-extensions -fms-compatibility -emit-llvm -o - %s | \
// RUN:   FileCheck %s --check-prefixes=CHECK,X64
// RUN: %clang_cc1 -x c++ -triple x86_64-pc-windows-msvc -ffreestanding \
// RUN:   -fms-extensions -fms-compatibility -emit-llvm -o - %s | \
// RUN:   FileCheck %s --check-prefixes=CHECK,X64

typedef __SIZE_TYPE__ size_t;
#include <intrin.h>
#include <immintrin.h>

#ifdef __cplusplus
using FxsaveType = void(__cdecl *)(void *);
using FxrstorType = void(__cdecl *)(void const *);
using XsaveType = void(__cdecl *)(void *, unsigned __int64);
using XrstorType = void(__cdecl *)(void const *, unsigned __int64);
static_assert(__is_same(decltype(&_fxsave), FxsaveType), "");
static_assert(__is_same(decltype(&_fxrstor), FxrstorType), "");
static_assert(__is_same(decltype(&_xsave), XsaveType), "");
static_assert(__is_same(decltype(&_xrstor), XrstorType), "");
extern "C" {
#endif

// CHECK-LABEL: define{{.*}} void @test_fxstate(
// CHECK-SAME: #[[FXATTR:[0-9]+]] {
// CHECK: call void @llvm.x86.fxsave(
// CHECK: call void @llvm.x86.fxrstor(
// X64: call void @llvm.x86.fxsave64(
// X64: call void @llvm.x86.fxrstor64(
// CHECK-NOT: asm
// CHECK: ret void
void __cdecl test_fxstate(void *save, void const *restore) {
  _fxsave(save);
  _fxrstor(restore);
#ifdef __x86_64__
  _fxsave64(save);
  _fxrstor64(restore);
#endif
}

// CHECK-LABEL: define{{.*}} void @test_xsave(
// CHECK-SAME: #[[XSAVEATTR:[0-9]+]] {
// CHECK: call void @llvm.x86.xsave(
// CHECK: call void @llvm.x86.xrstor(
// X64: call void @llvm.x86.xsave64(
// X64: call void @llvm.x86.xrstor64(
// CHECK: call i64 @llvm.x86.xgetbv(
// CHECK: call void @llvm.x86.xsetbv(
// CHECK-NOT: asm
// CHECK: ret void
void __cdecl test_xsave(void *save, void const *restore,
                        unsigned __int64 mask) {
  _xsave(save, mask);
  _xrstor(restore, mask);
#ifdef __x86_64__
  _xsave64(save, mask);
  _xrstor64(restore, mask);
#endif
  unsigned __int64 xcr0 = _xgetbv(_XCR_XFEATURE_ENABLED_MASK);
  _xsetbv(_XCR_XFEATURE_ENABLED_MASK, xcr0);
}

// CHECK-LABEL: define{{.*}} void @test_xsavec(
// CHECK-SAME: #[[XSAVECATTR:[0-9]+]] {
// CHECK: call void @llvm.x86.xsavec(
// X64: call void @llvm.x86.xsavec64(
// CHECK-NOT: asm
// CHECK: ret void
void __cdecl test_xsavec(void *save, unsigned __int64 mask) {
  _xsavec(save, mask);
#ifdef __x86_64__
  _xsavec64(save, mask);
#endif
}

// CHECK-LABEL: define{{.*}} void @test_xsaveopt(
// CHECK-SAME: #[[XSAVEOPTATTR:[0-9]+]] {
// CHECK: call void @llvm.x86.xsaveopt(
// X64: call void @llvm.x86.xsaveopt64(
// CHECK-NOT: asm
// CHECK: ret void
void __cdecl test_xsaveopt(void *save, unsigned __int64 mask) {
  _xsaveopt(save, mask);
#ifdef __x86_64__
  _xsaveopt64(save, mask);
#endif
}

// CHECK-LABEL: define{{.*}} void @test_xsaves(
// CHECK-SAME: #[[XSAVESATTR:[0-9]+]] {
// CHECK: call void @llvm.x86.xsaves(
// CHECK: call void @llvm.x86.xrstors(
// X64: call void @llvm.x86.xsaves64(
// X64: call void @llvm.x86.xrstors64(
// CHECK-NOT: asm
// CHECK: ret void
void __cdecl test_xsaves(void *save, void const *restore,
                         unsigned __int64 mask) {
  _xsaves(save, mask);
  _xrstors(restore, mask);
#ifdef __x86_64__
  _xsaves64(save, mask);
  _xrstors64(restore, mask);
#endif
}

#ifdef __x86_64__
// X64-LABEL: define{{.*}} i32 @test_rdrand64(
// X64-SAME: #[[RDRANDATTR:[0-9]+]] {
// X64: call { i64, i32 } @llvm.x86.rdrand.64()
// X64: store i64
// X64: ret i32
int __cdecl test_rdrand64(unsigned __int64 *value) {
  return _rdrand64_step(value);
}

// X64-LABEL: define{{.*}} i8 @test_cx16(
// X64-SAME: #[[CX16ATTR:[0-9]+]] {
// X64: cmpxchg volatile ptr
// X64: ret i8
unsigned char __cdecl test_cx16(__int64 volatile *destination,
                                __int64 exchange_high, __int64 exchange_low,
                                __int64 *comparand) {
  return _InterlockedCompareExchange128(destination, exchange_high,
                                        exchange_low, comparand);
}
#endif

#ifdef __cplusplus
}
#endif

// CHECK: attributes #[[FXATTR]] = {{.*"target-features"="[^"]*\+fxsr[^"]*".*}}
// CHECK: attributes #[[XSAVEATTR]] = {{.*"target-features"="[^"]*\+xsave[^"]*".*}}
// CHECK: attributes #[[XSAVECATTR]] = {{.*"target-features"="[^"]*\+xsavec[^"]*".*}}
// CHECK: attributes #[[XSAVEOPTATTR]] = {{.*"target-features"="[^"]*\+xsaveopt[^"]*".*}}
// CHECK: attributes #[[XSAVESATTR]] = {{.*"target-features"="[^"]*\+xsaves[^"]*".*}}
// X64: attributes #[[RDRANDATTR]] = {{.*"target-features"="[^"]*\+rdrnd[^"]*".*}}
// X64: attributes #[[CX16ATTR]] = {{.*"target-features"="[^"]*\+cx16[^"]*".*}}
