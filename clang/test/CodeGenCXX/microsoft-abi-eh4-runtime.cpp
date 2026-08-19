// REQUIRES: system-windows, ms-sdk
// REQUIRES: target=x86_64-{{.*}}-windows-msvc
// RUN: %clang_cl /nologo /EHsc /std:c++17 /O2 /d2FH4 /MD \
// RUN:   /Fe%t-md.exe -- %s
// RUN: %t-md.exe
// RUN: %t-md.exe terminate
// RUN: llvm-readobj --coff-imports %t-md.exe \
// RUN:   | FileCheck %s --check-prefix=MD
// RUN: %clang_cl /nologo /EHsc /std:c++17 /O2 /d2FH4 /MT \
// RUN:   /Fe%t-mt.exe -- %s
// RUN: %t-mt.exe
// RUN: %t-mt.exe terminate
// RUN: llvm-readobj --coff-imports %t-mt.exe \
// RUN:   | FileCheck %s --check-prefix=MT

#include <exception>
#include <stdlib.h>

static volatile int destructionOrder;

struct Tracked {
  int value;
  ~Tracked() { destructionOrder = destructionOrder * 10 + value; }
};

struct Payload {
  int value;
};

__declspec(noinline) static void throwInt(int value) { throw value; }
__declspec(noinline) static void throwPayload(int value) {
  throw Payload{value};
}

static bool testTypedCatchAndDirectCleanup() {
  destructionOrder = 0;
  try {
    Tracked object{7};
    throwInt(41);
  } catch (int value) {
    return value == 41 && destructionOrder == 7;
  }
  return false;
}

static bool testNestedCatchAndRethrow() {
  destructionOrder = 0;
  try {
    Tracked outer{2};
    try {
      Tracked inner{3};
      throwPayload(19);
    } catch (const Payload &value) {
      if (value.value != 19 || destructionOrder != 3)
        return false;
      throw;
    }
  } catch (const Payload &value) {
    return value.value == 19 && destructionOrder == 32;
  }
  return false;
}

static bool testCatchAll() {
  try {
    throwPayload(23);
  } catch (int) {
    return false;
  } catch (...) {
    return true;
  }
  return false;
}

static int catchContinuation(bool first) {
  try {
    throwInt(first ? 1 : 0);
  } catch (int value) {
    if (value)
      return 11;
  }
  return 22;
}

static void onTerminate() { _Exit(0); }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexceptions"
__declspec(noinline) static void escapeNoexcept() noexcept { throw 1; }
#pragma clang diagnostic pop

int main(int argc, char **) {
  if (argc > 1) {
    std::set_terminate(onTerminate);
    try {
      escapeNoexcept();
    } catch (...) {
      return 4;
    }
    return 5;
  }

  if (!testTypedCatchAndDirectCleanup())
    return 1;
  if (!testNestedCatchAndRethrow())
    return 2;
  if (!testCatchAll())
    return 3;
  if (catchContinuation(true) != 11 || catchContinuation(false) != 22)
    return 4;
  return 0;
}

// MD: Name: VCRUNTIME140_1.dll
// MD: Symbol: __CxxFrameHandler4
// MT: Format: COFF-x86-64
// MT-NOT: VCRUNTIME140_1.dll
