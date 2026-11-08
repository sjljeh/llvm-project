// REQUIRES: alpha-registered-target, mips-registered-target
// REQUIRES: powerpc-registered-target
//
// Alpha, MIPS and PowerPC targets for MSVC 13.0 (VC 7.0) or later use NTUW:
// __C_specific_handler with image-relative scope tables, and filters and
// termination handlers that take the establisher frame as an ordinary second
// argument. clang's default MSVC version selects NTUW.
//
// RUN: %clang -target mipsel-pc-windows-msvc -fms-extensions -O1 -S \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -O1 -S \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -O1 -S \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefixes=IR,PPC-IR
// RUN: %clang -target mipsel-pc-windows-msvc -fms-extensions -O1 -S \
// RUN:   -o - %s | FileCheck %s --check-prefixes=ASM,MIPS
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fms-extensions -O1 -S \
// RUN:   -o - %s | FileCheck %s --check-prefixes=ASM,ALPHA
// RUN: %clang -target powerpcle-pc-windows-msvc -fms-extensions -O1 -S \
// RUN:   -o - %s | FileCheck %s --check-prefixes=ASM,PPC
// RUN: %clang -target mipsel-pc-windows-msvc -fms-extensions -O1 -c \
// RUN:   -o %t.obj %s
// RUN: llvm-readobj --unwind %t.obj | FileCheck %s --check-prefix=OBJ
//
// MSVC 12.0 (VC 6.0) and earlier keep NT 4.0's absolute scope tables and
// three-argument filters.
// RUN: %clang -target mipsel-pc-windows-msvc12.0 -fms-extensions -O1 -S \
// RUN:   -emit-llvm -o - %s | FileCheck %s --check-prefix=LEGACY
// RUN: %clang -target mipsel-pc-windows-msvc12.0 -fms-extensions -O1 -S \
// RUN:   -o - %s | FileCheck %s --check-prefix=LEGACY-ASM

void g(int *);
int filt(int);

int f(int x) {
  int local = x;
  __try {
    g(&local);
  } __except (filt(local)) {
    return local + 1;
  }
  __try {
    g(&local);
  } __finally {
    g(&local);
  }
  return 0;
}

// IR: define {{.*}} @f({{.*}} personality ptr @__C_specific_handler {
// IR: define internal i32 @"?filt$0@0@f@@"(ptr {{[^,]*}}%0, ptr {{[^,)]*}}%1)
// PPC-IR-NOT: llvm.read_register
// PPC-IR: call void @llvm.write_register.i32(metadata ![[R2:[0-9]+]], i32 ptrtoint (ptr @.toc to i32))
// PPC-IR: ![[R2]] = !{!"r2"}

// ASM:      .seh_handler __C_specific_handler, @unwind, @except
// ASM:      .seh_handlerdata
// ASM:      .long {{.*}}@IMGREL # LabelStart
// ASM-NEXT: .long {{.*}}@IMGREL # LabelEnd
// MIPS-NEXT:  .long "?filt$0@0@f@@"@IMGREL # FilterFunction
// ALPHA-NEXT: .long "?filt$0@0@f@@"@IMGREL # FilterFunction
// The PowerPC runtimes enter a filter at its code, not its descriptor.
// PPC-NEXT:   .long "..?filt$0@0@f@@"@IMGREL # FilterFunction
// ASM-NEXT: .long {{.*}}@IMGREL # ExceptionHandler
// ASM:      .long "?dtor${{[0-9]+}}@?0?f@4HA"@IMGREL # FinallyFunclet

// The termination handler takes the establisher frame from a1.
// MIPS-LABEL: "?dtor${{[0-9]+}}@?0?f@4HA":
// MIPS:       move $fp, $5
// ALPHA-LABEL: "?dtor${{[0-9]+}}@?0?f@4HA":
// ALPHA:       lda $15,-{{[0-9]+}}($17)
// PPC-LABEL: "?dtor${{[0-9]+}}@?0?f@4HA":
// PPC:       addi 31, 4, -{{[0-9]+}}

// OBJ:      UnwindFormat: NTUW
// OBJ:      ExceptionHandler: __C_specific_handler+0x0
// OBJ:      CSpecificScopeTable {
// OBJ-NEXT:   Count: 2

// LEGACY: personality ptr @__C_specific_handler {
// LEGACY: define internal i32 @"?filt$0@0@f@@"(ptr {{[^,]*}}%0, ptr {{[^,]*}}%1, ptr {{[^,)]*}}%2)
// LEGACY-ASM:     .seh_handlerdata
// LEGACY-ASM-NOT: @IMGREL
// LEGACY-ASM:     .seh_endproc
