// RUN: %clang_cc1 -fcxx-exceptions -fexceptions -emit-llvm %s -triple x86_64-unknown-linux-gnu -o /dev/null -verify
// RUN: %clang_cc1 -fcxx-exceptions -fexceptions -fms-cxx-eh4 -emit-llvm %s -triple x86_64-pc-windows-msvc -o /dev/null -verify

// Negative: musttail to a non-noexcept callee from a noexcept function.

int ThrowingFunc(int);

int TestThrowingCallee(int x) noexcept {
  [[clang::musttail]] return ThrowingFunc(x); // expected-error {{'musttail' in a noexcept function requires a noexcept callee}}
}
