// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fexceptions -fcxx-exceptions -S %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fexceptions -fcxx-exceptions -c %s -o %t.obj
// RUN: llvm-readobj --sections --relocations --symbols %t.obj | FileCheck %s --check-prefix=OBJ
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -fexceptions -fcxx-exceptions -S %s -o %t.s
// RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %t.s -o %t.roundtrip.obj
// RUN: llvm-readobj --file-headers --sections --relocations %t.roundtrip.obj | FileCheck %s --check-prefix=ROUNDTRIP
// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -fexceptions -fcxx-exceptions -S %s -o %t.o2.s
// RUN: llvm-mc -triple=alpha-pc-windows-msvc -mattr=+taso -filetype=obj %t.o2.s -o %t.o2.obj

extern void may_throw();

int alpha_try_catch() {
  try {
    may_throw();
    return 0;
  } catch (...) {
    return 1;
  }
}

// ASM-LABEL: "?alpha_try_catch@@YAHXZ":
// ASM: .seh_proc "?alpha_try_catch@@YAHXZ"
// ASM: .seh_handler __CxxFrameHandler, @unwind, @except
// ASM: lda $30,-{{[0-9]+}}($30)
// ASM-NEXT: stq $26,{{[0-9]+}}($30)
// ASM-NEXT: .seh_endprologue
// ASM: .seh_handlerdata
// ASM: "$cppxdata$?alpha_try_catch@@YAHXZ":
// ASM: .long 429065504
// ASM: .long "?catch$1@?0??alpha_try_catch@@YAHXZ@4HA"
// ASM: .seh_endproc
// ASM: "?catch$1@?0??alpha_try_catch@@YAHXZ@4HA":
// ASM: .seh_proc "?catch$1@?0??alpha_try_catch@@YAHXZ@4HA"
// ASM: ldah $0,$ehgcr_0_3($31)
// ASM-NEXT: lda $0,$ehgcr_0_3($0)
// ASM-NEXT: ret
// ASM: $ehgcr_0_3:
// ASM: .seh_endproc

// OBJ: Name: .xdata
// OBJ: RawDataSize: 104
// OBJ: Name: .pdata
// OBJ: RawDataSize: 40
// OBJ: IMAGE_REL_ALPHA_REFHI $ehgcr_0_3
// OBJ-NEXT: IMAGE_REL_ALPHA_PAIR
// OBJ-NEXT: IMAGE_REL_ALPHA_REFLO $ehgcr_0_3
// OBJ: Section (4) .xdata
// OBJ: IMAGE_REL_ALPHA_REFLONG ?catch$1@?0??alpha_try_catch@@YAHXZ@4HA
// OBJ: Section (6) .pdata
// OBJ: IMAGE_REL_ALPHA_REFLONG __CxxFrameHandler
// OBJ: Name: ?catch$1@?0??alpha_try_catch@@YAHXZ@4HA
// OBJ: Section: .text (1)
// OBJ: Name: $ehgcr_0_3
// OBJ: Section: .text (1)

// ROUNDTRIP: Format: COFF-Alpha
// ROUNDTRIP: AddressSize: 32bit
// ROUNDTRIP: Name: .xdata
// ROUNDTRIP: Name: .pdata
// ROUNDTRIP: IMAGE_REL_ALPHA_BRADDR ?may_throw@@YAXXZ
// ROUNDTRIP: IMAGE_REL_ALPHA_REFHI $ehgcr_0_3
// ROUNDTRIP-NEXT: IMAGE_REL_ALPHA_PAIR
// ROUNDTRIP-NEXT: IMAGE_REL_ALPHA_REFLO $ehgcr_0_3
// ROUNDTRIP: Section (4) .xdata
// ROUNDTRIP: Section (6) .pdata
// ROUNDTRIP: IMAGE_REL_ALPHA_REFLONG __CxxFrameHandler
