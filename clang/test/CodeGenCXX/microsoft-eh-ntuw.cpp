// REQUIRES: alpha-registered-target, mips-registered-target
// REQUIRES: powerpc-registered-target
//
// NTUW C++ EH uses __CxxFrameHandler3 with
// image-relative tables. Catch and cleanup funclets receive the establisher
// frame as their second argument; a catch funclet saves it where dispFrame
// (ParentFrameOffset) says, and the parent starts UnwindHelp at -2. Each
// target's offsets below are checked against the code that uses them.
//
// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -O1 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -O1 -S -o - %s | FileCheck %s --check-prefix=MIPS
// RUN: %clangxx -target alpha-pc-windows-msvc -mtaso -fexceptions \
// RUN:   -fcxx-exceptions -O1 -S -o - %s | FileCheck %s --check-prefix=ALPHA
// RUN: %clangxx -target powerpcle-pc-windows-msvc -fexceptions \
// RUN:   -fcxx-exceptions -O1 -S -o - %s | FileCheck %s --check-prefix=PPC

struct D {
  ~D();
};
void g(int);

int f(int x) {
  D d;
  try {
    g(x);
  } catch (int e) {
    g(e);
    return e;
  }
  return 0;
}

// IR: personality ptr @__CxxFrameHandler3

// MIPS: .seh_handler __CxxFrameHandler3, @unwind, @except
// The frame is 48 bytes; $fp is the CFA - 48.
// MIPS:      move $fp, $sp
// MIPS:      addiu $1, $zero, -2
// MIPS-NEXT: sw $1, 36($fp)
// MIPS-LABEL: "?catch$2@?0??f@@YAHH@Z@4HA":
// MIPS:      sw $5, 44($sp)
// MIPS-NEXT: move $fp, $5
// MIPS:      lw $4, -8($fp)
// MIPS-LABEL: "$cppxdata$?f@@YAHH@Z":
// MIPS:      .long -12 # UnwindHelp
// MIPS:      .long -8 # CatchObjOffset
// MIPS-NEXT: .long "?catch$2@?0??f@@YAHH@Z@4HA"@IMGREL # Handler
// MIPS-NEXT: .long 4294967292 # ParentFrameOffset

// The frame is 64 bytes; $15 is the CFA - 64.
// ALPHA:      lda $28,-2($31)
// ALPHA-NEXT: stl $28,48($15)
// ALPHA-LABEL: "?catch$2@?0??f@@YAHH@Z@4HA":
// ALPHA:      stq $17,56($30)
// ALPHA-NEXT: lda $15,-64($17)
// ALPHA:      lda $9,24($15)
// ALPHA-LABEL: "$cppxdata$?f@@YAHH@Z":
// ALPHA:      .long -16 # UnwindHelp
// ALPHA:      .long -40 # CatchObjOffset
// ALPHA-NEXT: .long "?catch$2@?0??f@@YAHH@Z@4HA"@IMGREL # Handler
// ALPHA-NEXT: .long 4294967288 # ParentFrameOffset

// The frame is 80 bytes; r31 is the CFA - 80. The funclet takes its CFA from
// its back chain.
// PPC:      li 0, -2
// PPC:      stw 0, 64(31)
// PPC-LABEL: "?catch$2@?0??f@@YAHH@Z@4HA":
// PPC:      lwz 12, 0(1)
// PPC:      addi 31, 4, -80
// PPC:      stw 4, -12(12)
// PPC:      lwz 3, 72(31)
// PPC-LABEL: "$cppxdata$?f@@YAHH@Z":
// PPC:      .long -16 # UnwindHelp
// PPC:      .long -8 # CatchObjOffset
// PPC-NEXT: .long "?catch$2@?0??f@@YAHH@Z@4HA"@IMGREL # Handler
// PPC-NEXT: .long 4294967284 # ParentFrameOffset
