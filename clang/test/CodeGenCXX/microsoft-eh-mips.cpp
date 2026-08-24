// REQUIRES: mips-registered-target

// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR \
// RUN:   --implicit-check-not=__CxxFrameHandler3
// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --hex-dump=.xdata \
// RUN:   --hex-dump=.pdata %t.obj | FileCheck %s --check-prefix=OBJ \
// RUN:   --implicit-check-not=IMAGE_REL_MIPS_REFWORDNB
// RUN: %clangxx -target mipsel-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -o %t.s %s
// RUN: %clangxx -target mipsel-pc-windows-msvc -c -o %t.roundtrip.obj %t.s
// RUN: llvm-readobj --relocations --hex-dump=.pdata %t.roundtrip.obj \
// RUN:   | FileCheck %s --check-prefix=ROUNDTRIP \
// RUN:   --implicit-check-not=IMAGE_REL_MIPS_REFWORDNB

extern void f();
extern void use(int);

void g() {
  try {
    f();
  } catch (int value) {
    use(value);
  }
}

// IR-LABEL: define dso_local void @"?g@@YAXXZ"()
// IR-SAME: personality ptr @__CxxFrameHandler
// IR: declare dso_local i32 @__CxxFrameHandler(...)

// ASM: .seh_handler __CxxFrameHandler, @unwind, @except
// ASM: .seh_endprologue
// ASM: "$cppxdata$?g@@YAXXZ":
// ASM-NEXT: .long 429065504
// ASM: .long -1{{.*}}# TryBlockIndex
// ASM-NEXT: .long 1{{.*}}# FrameNestLevel
// ASM: "$handlerMap$0$?g@@YAXXZ":
// ASM: .long -8{{.*}}# CatchObjOffset
// ASM-NEXT: .long 1{{.*}}# FrameNestLevel
// ASM: "?catch$1@?0??g@@YAXXZ@4HA":
// ASM: sw $2, 28($sp)
// ASM-NEXT: move $fp, $2
// ASM-NEXT: .seh_endprologue
// ASM: lw $4, -8($fp)
// ASM: lui $2, %hi(.LBB0_2)
// ASM-NEXT: addiu $2, $2, %lo(.LBB0_2)
// ASM-NEXT: jr $ra
// ASM: "$cppxdata$?catch$1@?0??g@@YAXXZ@4HA":
// ASM: .long 0{{.*}}# TryBlockIndex
// ASM-NEXT: .long 2{{.*}}# FrameNestLevel

// OBJ: Format: COFF-MIPS
// OBJ: Name: .xdata
// OBJ: RawDataSize: 176
// OBJ: Name: .pdata
// OBJ: RawDataSize: 40
// OBJ: Section {{.*}} .xdata {
// OBJ: IMAGE_REL_MIPS_REFWORD $stateUnwindMap$?g@@YAXXZ
// OBJ: IMAGE_REL_MIPS_REFWORD $ip2state$?catch$1@?0??g@@YAXXZ@4HA
// OBJ: Section {{.*}} .pdata {
// OBJ: IMAGE_REL_MIPS_REFWORD __CxxFrameHandler
// OBJ-NEXT: IMAGE_REL_MIPS_REFWORD $cppxdata$?g@@YAXXZ
// OBJ: IMAGE_REL_MIPS_REFWORD __CxxFrameHandler
// OBJ-NEXT: IMAGE_REL_MIPS_REFWORD $cppxdata$?catch$1@?0??g@@YAXXZ@4HA
// OBJ: Hex dump of section '.xdata':
// OBJ-NEXT: 0x00000000 20059319
// OBJ: 0x00000020 ffffffff 01000000
// OBJ: 0x00000080 20059319

// ROUNDTRIP: Section {{.*}} .pdata {
// ROUNDTRIP: IMAGE_REL_MIPS_REFWORD __CxxFrameHandler
// ROUNDTRIP-NEXT: IMAGE_REL_MIPS_REFWORD .xdata
// ROUNDTRIP: IMAGE_REL_MIPS_REFWORD __CxxFrameHandler
// ROUNDTRIP-NEXT: IMAGE_REL_MIPS_REFWORD .xdata
// ROUNDTRIP: Hex dump of section '.pdata':
// ROUNDTRIP: 0x00000020 80000000 5c000000
