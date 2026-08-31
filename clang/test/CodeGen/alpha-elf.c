// RUN: %clang_cc1 -triple alpha-unknown-linux-gnu -mrelocation-model static -debug-info-kind=limited -emit-obj %s -o - | llvm-readobj --file-headers --sections --symbols --relocations - | FileCheck %s

static __thread long tls;
extern long ext(long);
long global;

long f(long x) {
  tls += x;
  global = ext(tls);
  return global;
}

// CHECK: Format: elf64-alpha
// CHECK: Machine: EM_ALPHA (0x9026)
// CHECK: Name: .debug_info
// CHECK: Name: .rela.debug_info
// CHECK: R_ALPHA_GPDISP - 0x4
// CHECK: R_ALPHA_TPRELHI tls 0x0
// CHECK: R_ALPHA_TPRELLO tls 0x0
// CHECK: R_ALPHA_LITERAL ext 0x0
// CHECK: R_ALPHA_LITUSE - 0x3
// CHECK: R_ALPHA_LITERAL global 0x0
// CHECK: R_ALPHA_REFLONG .debug_abbrev 0x0
// CHECK: Name: f
// CHECK: Other [ (0x88)
