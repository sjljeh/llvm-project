// REQUIRES: powerpc-registered-target
//
// RUN: not %clang_cc1 -triple powerpcle-pc-windows-gnu -fms-extensions \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=GNU
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O0 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O0 -S -o - %s | FileCheck %s --check-prefix=ASM
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O0 -c -o %t.obj %s
// RUN: llvm-readobj --sections --relocations --hex-dump=.xdata \
// RUN:   --hex-dump=.pdata %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O2 -ffunction-sections -c -o %t.o2.obj %s
// RUN: llvm-readobj --symbols %t.o2.obj | FileCheck %s --check-prefix=COMDAT
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -fexceptions \
// RUN:   -O0 -S -o %t.s %s
// RUN: %clang -target powerpcle-pc-windows-msvc -c -o %t.roundtrip.obj %t.s
// RUN: llvm-readobj --relocations %t.roundtrip.obj \
// RUN:   | FileCheck %s --check-prefix=ROUNDTRIP

// GNU: error: SEH '__try' is not supported on this target

extern void may_fault(void);
extern int inspect_exception(unsigned code, int selector);

int seh_filter(volatile int *p, int selector) {
  int value = selector + 7;
  __try {
    value += *p;
    may_fault();
  } __except (inspect_exception(__exception_code(), value)) {
    value = -value;
  }
  return value;
}

// IR-LABEL: define dso_local i32 @seh_filter(
// IR-SAME: personality ptr @__C_specific_handler
// IR: call void (...) @llvm.localescape(
// IR: catchpad within {{.*}} [ptr @"?filt$0@0@seh_filter@@"]
// IR-LABEL: define internal i32 @"?filt$0@0@seh_filter@@"(
// The NT PowerPC filter helper receives the exception pointers in r3 and the
// establisher's incoming SP in r2. It restores the parent's TOC from the
// linkage area before evaluating a filter that may make calls.
// IR: call i32 @llvm.read_register.i32(metadata [[R2:![0-9]+]])
// IR: getelementptr inbounds i8, ptr {{.*}}, i32 8
// IR: call void @llvm.write_register.i32(metadata [[R2]], i32 {{.*}})
// IR: call ptr @llvm.eh.recoverfp(ptr @seh_filter, ptr {{.*}})
// IR: call ptr @llvm.localrecover(ptr @seh_filter, ptr {{.*}}, i32 0)
// IR: call i32 @inspect_exception(
// IR: [[R2]] = !{!"r2"}

// The parent follows the VC4 layout: save r2 in the incoming linkage area,
// establish a frame, and publish an absolute-address C scope table.
// ASM-LABEL: ..seh_filter:
// ASM: .seh_handler __C_specific_handler, @unwind, @except
// ASM: stw 2, 8(1)
// ASM: stwu 1, -80(1)
// ASM: mr 31, 1
// ASM: .seh_endprologue
// ASM: .Lseh_filter$frame_escape_0 = 64
// ASM: .Lseh_filter$parent_frame_offset = -80
// ASM: .long {{.*}} # Number of call sites
// ASM: .long {{.*}} # LabelStart
// ASM-NEXT: .long {{.*}} # LabelEnd
// The scope record must contain the direct code entry, not the descriptor.
// ASM-NEXT: .long "..?filt$0@0@seh_filter@@" # FilterFunction
// ASM-NEXT: .long {{.*}} # ExceptionHandler

// The filter materializes the parent-frame and local-capture offsets, restores
// the TOC from r2, and then calls the source filter expression.
// ASM-LABEL: "..?filt$0@0@seh_filter@@":
// ASM: mr 3, 2
// ASM-NEXT: lwz 4, 8(3)
// ASM-NEXT: mr 2, 4
// ASM: lis 4, .Lseh_filter$parent_frame_offset@ha
// ASM-NEXT: addi 4, 4, .Lseh_filter$parent_frame_offset@l
// ASM-NEXT: add 3, 3, 4
// ASM: lis 4, .Lseh_filter$frame_escape_0@ha
// ASM-NEXT: addi 4, 4, .Lseh_filter$frame_escape_0@l
// ASM: bl ..inspect_exception

// OBJ: Format: COFF-PowerPC
// OBJ: Name: .xdata
// OBJ: RawDataSize: 20
// OBJ: Name: .pdata
// OBJ: RawDataSize: 40
// OBJ: Section {{.*}} .xdata {
// OBJ: 0xC IMAGE_REL_PPC_ADDR32 ..?filt$0@0@seh_filter@@
// OBJ: Section {{.*}} .pdata {
// OBJ: 0x8 IMAGE_REL_PPC_ADDR32 __C_specific_handler
// OBJ-NEXT: 0xC IMAGE_REL_PPC_ADDR32 .xdata
// OBJ: Hex dump of section '.xdata':
// OBJ-NEXT: 0x00000000 01000000 30000000 4c000000 00000000
// OBJ-NEXT: 0x00000010 54000000

// ROUNDTRIP: Section {{.*}} .xdata {
// ROUNDTRIP: 0xC IMAGE_REL_PPC_ADDR32 ..?filt$0@0@seh_filter@@
// ROUNDTRIP: Section {{.*}} .pdata {
// ROUNDTRIP: 0x8 IMAGE_REL_PPC_ADDR32 __C_specific_handler

// COMDAT: Name: .xdata
// COMDAT: Selection: Associative (0x5)
// COMDAT: Name: .pdata
// COMDAT: Selection: Associative (0x5)
