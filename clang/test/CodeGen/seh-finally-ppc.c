// REQUIRES: powerpc-registered-target
//
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O0 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O0 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O0 -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --hex-dump=.xdata \
// RUN:   --hex-dump=.pdata %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O2 -c -o %t.o2.obj %s

extern void may_fault(void);
extern void consume(int);

int seh_finally(volatile int *p) {
  int value = 0;
  __try {
    value = *p;
    may_fault();
  } __finally {
    value += __abnormal_termination() ? 3 : 2;
    consume(value);
  }
  return value;
}

// IR-LABEL: define dso_local i32 @seh_finally(
// IR-SAME: personality ptr @__C_specific_handler
// IR: call void (...) @llvm.localescape(
// IR: cleanuppad within none
// IR: call void @"?fin$0@0@seh_finally@@"(i8 noundef zeroext 1,
// IR-LABEL: define internal void @"?fin$0@0@seh_finally@@"(
// IR: call ptr @llvm.localrecover(ptr @seh_finally, ptr {{.*}}, i32 0)
// IR: %{{.*}} = zext i8 %0 to i32
// IR: call void @consume(

// The parent save and table format follow NT4 PowerPC. The unwind path enters
// a real PPC funclet, which derives the parent frame from r2 and restores the
// parent TOC before calling the outlined body.
// ASM-LABEL: ..seh_finally:
// ASM: .seh_handler __C_specific_handler, @unwind, @except
// ASM: stw 2, 8(1)
// ASM: stwu 1, -80(1)
// ASM: mr 31, 1
// ASM: .seh_endprologue
// ASM: .Lseh_finally$frame_escape_0 = 68
// ASM: .Lseh_finally$parent_frame_offset = -80
// ASM: .long {{.*}} # Number of call sites
// ASM: .long {{.*}} # LabelStart
// ASM-NEXT: .long {{.*}} # LabelEnd
// ASM-LABEL: "?dtor$
// ASM: stwu 1, -80(1)
// ASM: addi 31, 2, -80
// ASM-NEXT: lwz 2, 8(2)
// ASM-NEXT: .seh_endprologue
// ASM: mr 4, 31
// ASM: bl "..?fin$0@0@seh_finally@@"

// The outlined body recovers the escaped local through an assembler-time
// offset that is emitted as a normal PPC @ha/@l pair.
// ASM-LABEL: "..?fin$0@0@seh_finally@@":
// ASM: lis {{[0-9]+}}, .Lseh_finally$frame_escape_0@ha
// ASM-NEXT: addi {{[0-9]+}}, {{[0-9]+}}, .Lseh_finally$frame_escape_0@l
// ASM: bl ..consume

// OBJ: Format: COFF-PowerPC
// OBJ: Name: .xdata
// OBJ: RawDataSize: 20
// OBJ: Name: .pdata
// OBJ: RawDataSize: 60
// OBJ: Section {{.*}} .xdata {
// OBJ: IMAGE_REL_PPC_ADDR32 ?dtor$
// OBJ: Section {{.*}} .pdata {
// OBJ: 0x8 IMAGE_REL_PPC_ADDR32 __C_specific_handler
// OBJ-NEXT: 0xC IMAGE_REL_PPC_ADDR32 .xdata
