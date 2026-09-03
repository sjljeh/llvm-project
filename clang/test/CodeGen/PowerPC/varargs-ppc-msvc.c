// REQUIRES: powerpc-registered-target
// RUN: %clang_cc1 -triple powerpcle-pc-windows-msvc -emit-llvm -O0 -o - %s \
// RUN:   | FileCheck %s --check-prefix=MS
// RUN: %clang_cc1 -triple powerpc-unknown-linux-gnu -emit-llvm -O0 -o - %s \
// RUN:   | FileCheck %s --check-prefix=ELF

typedef struct {
  int a;
  short b;
} Pair;

// MS-LABEL: define dso_local double @consume(i32 noundef %fixed, ...)
// MS: %ap = alloca ptr, align 4
// MS: call void @llvm.va_start.p0(ptr %ap)
// MS: %[[INT_CUR:.*]] = load ptr, ptr %ap, align 4
// MS: %[[INT_NEXT:.*]] = getelementptr inbounds i8, ptr %[[INT_CUR]], i32 4
// MS: store ptr %[[INT_NEXT]], ptr %ap, align 4
// MS: load i32, ptr %[[INT_CUR]], align 4
// MS: %[[DOUBLE_CUR:.*]] = load ptr, ptr %ap, align 4
// MS: %[[DOUBLE_BIASED:.*]] = getelementptr inbounds i8, ptr %[[DOUBLE_CUR]], i32 7
// MS: %[[DOUBLE_ALIGNED:.*]] = call ptr @llvm.ptrmask.p0.i32(ptr %[[DOUBLE_BIASED]], i32 -8)
// MS: %[[DOUBLE_NEXT:.*]] = getelementptr inbounds i8, ptr %[[DOUBLE_ALIGNED]], i32 8
// MS: store ptr %[[DOUBLE_NEXT]], ptr %ap, align 4
// MS: load double, ptr %[[DOUBLE_ALIGNED]], align 8
// MS: %[[I64_CUR:.*]] = load ptr, ptr %ap, align 4
// MS: %[[I64_BIASED:.*]] = getelementptr inbounds i8, ptr %[[I64_CUR]], i32 7
// MS: %[[I64_ALIGNED:.*]] = call ptr @llvm.ptrmask.p0.i32(ptr %[[I64_BIASED]], i32 -8)
// MS: %[[I64_NEXT:.*]] = getelementptr inbounds i8, ptr %[[I64_ALIGNED]], i32 8
// MS: store ptr %[[I64_NEXT]], ptr %ap, align 4
// MS: load i64, ptr %[[I64_ALIGNED]], align 8
// MS: %[[PAIR_CUR:.*]] = load ptr, ptr %ap, align 4
// MS: %[[PAIR_BIASED:.*]] = getelementptr inbounds i8, ptr %[[PAIR_CUR]], i32 7
// MS: %[[PAIR_ALIGNED:.*]] = call ptr @llvm.ptrmask.p0.i32(ptr %[[PAIR_BIASED]], i32 -8)
// MS: %[[PAIR_NEXT:.*]] = getelementptr inbounds i8, ptr %[[PAIR_ALIGNED]], i32 8
// MS: store ptr %[[PAIR_NEXT]], ptr %ap, align 4
// MS: call void @llvm.memcpy.p0.p0.i32(ptr align 4 %pair, ptr align 8 %[[PAIR_ALIGNED]], i32 8, i1 false)
// ELF-LABEL: define dso_local double @consume
// ELF: %ap = alloca [1 x %struct.__va_list_tag], align 4
// ELF: call void @llvm.va_start.p0(ptr %arraydecay)
double consume(int fixed, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, fixed);
  int i = __builtin_va_arg(ap, int);
  double d = __builtin_va_arg(ap, double);
  long long q = __builtin_va_arg(ap, long long);
  Pair pair = __builtin_va_arg(ap, Pair);
  __builtin_va_end(ap);
  return fixed + i + d + q + pair.a + pair.b;
}

extern void sink(int, ...);

// A second double needs an explicit skipped word after the intervening int.
// Plain C aggregates are inline in the same stream rather than passed by a
// pointer as in the PPC32 SVR4 ABI.
// MS-LABEL: define dso_local void @call_variadic()
// MS: call void (i32, ...) @sink(i32 noundef 1, i32 noundef 2, double noundef 3.000000e+00, i32 noundef 4, i32 undef, double noundef 5.000000e+00, i64 noundef 1234605616436508552, i32 inreg %{{.*}}, i32 inreg %{{.*}}, i32 noundef 8)
void call_variadic(void) {
  Pair pair = {6, 7};
  sink(1, 2, 3.0, 4, 5.0, 0x1122334455667788LL, pair, 8);
}

// MS-LABEL: define dso_local void @call_i64()
// MS: call void (i32, ...) @sink(i32 noundef 1, i32 undef, i64 noundef 1234605616436508552, i32 noundef 2)
void call_i64(void) {
  sink(1, 0x1122334455667788LL, 2);
}

// MS-LABEL: define dso_local void @take_pair(i32 inreg %pair.coerce0, i32 inreg %pair.coerce1)
void take_pair(Pair pair) {
  (void)pair;
}
