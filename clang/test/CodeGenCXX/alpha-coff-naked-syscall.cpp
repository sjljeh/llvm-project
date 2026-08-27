// RUN: %clang -target alpha-pc-windows-msvc -mtaso -O2 -x c++ -std=c++14 -S %s -o - | FileCheck %s

typedef unsigned long long uint64_t;

template <int syscall_id, typename... arglist>
__attribute__((naked)) uint64_t syscall(arglist... args) {
  __asm__ volatile("lda $0,%0($31); call_pal 0x83; ret" : : "i"(syscall_id));
}

extern "C" uint64_t start() {
  return syscall<42, uint64_t, uint64_t, uint64_t>(1, 2, 3);
}

// CHECK-LABEL: start:
// CHECK: lda $16,1($31)
// CHECK: lda $17,2($31)
// CHECK: lda $18,3($31)
// CHECK: bsr $26,
// CHECK: ret
// CHECK: lda $0,42($31)
// CHECK-NEXT: call_pal 131
// CHECK-NEXT: ret
