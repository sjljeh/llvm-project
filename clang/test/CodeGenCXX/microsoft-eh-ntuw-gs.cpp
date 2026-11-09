// REQUIRES: alpha-registered-target, mips-registered-target
// REQUIRES: powerpc-registered-target
//
// /GS with C++ EH under NTUW: __GSHandlerCheck_EH stands in for
// __CxxFrameHandler3. Its handler data is the FuncInfo RVA, then the cookie
// slot's offset from the establisher frame and flags. Catch funclets keep
// __CxxFrameHandler3.
// RUN: %clang -target mipsel-pc-windows-msvc -O2 -fstack-protector-strong \
// RUN:   -fexceptions -fcxx-exceptions -S -o - %s | FileCheck %s
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -fstack-protector-strong \
// RUN:   -fexceptions -fcxx-exceptions -S -o - %s | FileCheck %s
// RUN: %clang -target powerpcle-pc-windows-msvc -O2 -fstack-protector-strong \
// RUN:   -fexceptions -fcxx-exceptions -S -o - %s | FileCheck %s

void use(char *);
struct D {
  ~D();
};

int cxx(int n) {
  char buf[64];
  try {
    D d;
    use(buf);
  } catch (int) {
    return -1;
  }
  return buf[n];
}

// CHECK:      .seh_handler __GSHandlerCheck_EH, @unwind, @except
// CHECK:      .seh_handlerdata
// CHECK-NEXT: .long "$cppxdata$?cxx@@YAHH@Z"@IMGREL
// CHECK-NEXT: .long {{-[0-9]+}} # GS cookie offset
// CHECK-NEXT: .long 0 # GS flags
// CHECK:      .seh_handler __CxxFrameHandler3, @unwind, @except
