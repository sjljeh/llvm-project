// RUN: %clang_cc1 -triple alpha-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

struct alpha_model {
  char c;
  long double ld;
  __int128 i;
};

_Static_assert(sizeof(void *) == 8, "ELF Alpha pointers must be 64-bit");
_Static_assert(sizeof(long) == 8, "ELF Alpha must use LP64");
_Static_assert(sizeof(long double) == 16, "long double must be IEEE quad");
_Static_assert(_Alignof(long double) == 16, "long double alignment");
_Static_assert(sizeof(__int128) == 16, "__int128 size");
_Static_assert(_Alignof(__int128) == 16, "__int128 alignment");
_Static_assert(sizeof(struct alpha_model) == 48, "aggregate layout");
_Static_assert(_Alignof(struct alpha_model) == 16, "aggregate alignment");

long global_long;
void *global_pointer;
long double global_long_double;
__int128 global_int128;
struct alpha_model global_model;

// CHECK: target datalayout = "e-m:e-p:64:64-i64:64-i128:128-f128:128:128-n64-S128"
// CHECK: target triple = "alpha-unknown-linux-gnu"
// CHECK: %struct.alpha_model = type { i8, fp128, i128 }
// CHECK-DAG: @global_long = global i64 0, align 8
// CHECK-DAG: @global_pointer = global ptr null, align 8
// CHECK-DAG: @global_long_double = global fp128 0.000000e+00, align 16
// CHECK-DAG: @global_int128 = global i128 0, align 16
// CHECK-DAG: @global_model = global %struct.alpha_model zeroinitializer, align 16
