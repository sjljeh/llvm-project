// REQUIRES: powerpc-registered-target
//
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -O0 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -O0 -c -o %t.O0.obj %s
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -O2 -c -o %t.O2.obj %s

struct E {
  int value;
  ~E();
};
struct Guard {
  ~Guard();
};

E::~E() {}
Guard::~Guard() {}
void sink(int) {}

void inner(int mode) {
  Guard guard;
  try {
    if (mode)
      throw E{mode};
    throw 7;
  } catch (const E &e) {
    sink(e.value);
    if (e.value == 2)
      throw;
  } catch (int value) {
    sink(value);
  }
}

// ASM: "$tryMap$?inner@@YAXH@Z":
// ASM: .long 2{{.*}}# NumCatches
// ASM-NEXT: .long "$handlerMap$0$?inner@@YAXH@Z"{{.*}}# HandlerArray
// ASM-NEXT: "$handlerMap$0$?inner@@YAXH@Z":
// ASM-NEXT: .long 8{{.*}}# Adjectives
// ASM-NEXT: .long "??_R0?AUE@@@8"{{.*}}# Type
// ASM-NEXT: .long -4{{.*}}# CatchObjOffset
// ASM-NEXT: .long "?catch$7@?0??inner@@YAXH@Z@4HA"{{.*}}# Handler
// ASM-NEXT: .long 0{{.*}}# Adjectives
// ASM-NEXT: .long "??_R0H@8"{{.*}}# Type
// ASM-NEXT: .long -8{{.*}}# CatchObjOffset
// ASM-NEXT: .long "?catch$11@?0??inner@@YAXH@Z@4HA"{{.*}}# Handler

// Both catch funclets and the destructor cleanup receive the establisher frame
// in r2. A rethrow from the first catch calls the VC4 throw helper with null
// object and ThrowInfo pointers.
// ASM: "?catch$7@?0??inner@@YAXH@Z@4HA":
// ASM: addi 31, 2, -80
// ASM-NEXT: lwz 2, 8(2)
// ASM: li 4, 0
// ASM-NEXT: mr 3, 4
// ASM-NEXT: bl .._CxxThrowException
// ASM: "?catch$11@?0??inner@@YAXH@Z@4HA":
// ASM: addi 31, 2, -80
// ASM-NEXT: lwz 2, 8(2)
// ASM: "?dtor$12@?0??inner@@YAXH@Z@4HA":
// ASM: addi 31, 2, -80
// ASM-NEXT: lwz 2, 8(2)
