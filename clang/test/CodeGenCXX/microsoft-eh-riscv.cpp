// REQUIRES: riscv-registered-target

// RUN: %clangxx -target riscv64-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clangxx -target riscv64-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clangxx -target riscv64-pc-windows-msvc -fexceptions -fcxx-exceptions \
// RUN:   -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --unwind %t.obj | \
// RUN:   FileCheck %s --check-prefix=OBJ

extern void f();

void g() {
  try {
    f();
  } catch (int) {
  }
}

// IR-LABEL: define dso_local void @"?g@@YAXXZ"()
// IR-SAME: personality ptr @__CxxFrameHandler3
// IR: declare dso_local i32 @__CxxFrameHandler3(...)

// ASM: .seh_proc "?g@@YAXXZ"
// ASM: .seh_handler __CxxFrameHandler3, @unwind, @except
// ASM: .seh_set_cfa x2, 16
// ASM: .seh_save_gpr x1, -8
// ASM: .seh_endprologue
// ASM: .seh_proc "?catch$1@?0??g@@YAXXZ@4HA"
// ASM: auipc a0, %pcrel_hi($ehgcr_0_2)
// ASM: addi a0, a0, %pcrel_lo(
// ASM: ret
// ASM: "$cppxdata$?g@@YAXXZ":

// OBJ: Format: COFF-RISCV64
// OBJ: Name: .xdata
// OBJ: Name: .pdata
// OBJ: Section {{.*}} .pdata {
// OBJ: IMAGE_REL_RISCV_ADDR32NB .text
// OBJ: IMAGE_REL_RISCV_ADDR32NB .xdata
// OBJ: UnwindInformation [
// OBJ: RuntimeFunction {
// OBJ: UnwindInfo {
// OBJ: Version: 1
// OBJ-NOT: CSpecificScopeTable
