// REQUIRES: system-windows, ms-sdk
// REQUIRES: target=x86_64-{{.*}}-windows-msvc
// RUN: split-file %s %t
// RUN: %clang_cl /nologo /EHsc /O2 /d2FH4 /MT /c /Fo%t/callee.obj \
// RUN:   -- %t/callee.cpp
// RUN: %clang_cl /nologo /EHsc /O2 /d2FH4 /MT /c /Fo%t/caller.obj \
// RUN:   -- %t/caller.cpp
// RUN: %clang_cl /nologo /MT /Fe%t/test.exe \
// RUN:   -- %t/callee.obj %t/caller.obj
// RUN: %t/test.exe

//--- callee.cpp
volatile int throwNow = 1;

__declspec(noinline) static void throwInt() {
  if (throwNow)
    throw 7;
}

__forceinline static void contract() noexcept { throwInt(); }

__declspec(noinline) void bridge() { contract(); }

//--- caller.cpp
#include <exception>
#include <stdlib.h>

void bridge();

[[noreturn]] static void terminated() { _Exit(0); }

int main() {
  std::set_terminate(terminated);
  try {
    bridge();
  } catch (...) {
    return 1;
  }
  return 2;
}
