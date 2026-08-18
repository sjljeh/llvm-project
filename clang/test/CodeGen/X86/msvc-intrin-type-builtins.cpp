// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fms-compatibility -ast-dump -fsyntax-only %s | \
// RUN:   FileCheck %s --check-prefix=AST
// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fms-compatibility -emit-llvm -o - %s | FileCheck %s --check-prefix=IR
// RUN: %clang_cc1 -triple i686-pc-windows-msvc -fms-extensions \
// RUN:   -fms-compatibility -emit-llvm -o - %s | FileCheck %s --check-prefix=IR

typedef union __declspec(intrin_type) __declspec(align(16)) __m128 {
  float f32[4];
} __m128;

typedef struct __declspec(intrin_type) __declspec(align(16)) __m128d {
  double f64[2];
} __m128d;

typedef union __declspec(intrin_type) __declspec(align(16)) __m128i {
  char i8[16];
  short i16[8];
  int i32[4];
  long long i64[2];
} __m128i;

typedef __declspec(align(1)) __m128i __m128i_u;

extern "C" {
__m128 _mm_load_ss(float const *);
__m128 _mm_sqrt_ss(__m128);
void _mm_store_ss(float *, __m128);

__m128d _mm_load_sd(double const *);
__m128d _mm_setzero_pd(void);
__m128d _mm_sqrt_sd(__m128d, __m128d);
void _mm_store_sd(double *, __m128d);

__m128i _mm_cmpeq_epi16(__m128i, __m128i);
__m128i _mm_cmpeq_epi8(__m128i, __m128i);
__m128i _mm_cvtsi32_si128(int);
__m128i _mm_loadu_si128(__m128i_u const *);
int _mm_movemask_epi8(__m128i);
__m128i _mm_or_si128(__m128i, __m128i);
__m128i _mm_setzero_si128(void);
__m128i _mm_shuffle_epi32(__m128i, int);
__m128i _mm_slli_si128(__m128i, int);
__m128i _mm_srli_si128(__m128i, int);
void _mm_storeu_si128(__m128i_u *, __m128i);
__m128i _mm_unpackhi_epi16(__m128i, __m128i);
__m128i _mm_unpackhi_epi8(__m128i, __m128i);
__m128i _mm_unpacklo_epi16(__m128i, __m128i);
__m128i _mm_unpacklo_epi8(__m128i, __m128i);
__m128i _mm_xor_si128(__m128i, __m128i);
}

// AST: FunctionDecl {{.*}} _mm_load_ss
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_sqrt_sd
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_xor_si128
// AST: BuiltinAttr {{.*}} Implicit

extern "C" {

__m128 test_load_ss(float const *p) { return _mm_load_ss(p); }
// IR-LABEL: define {{.*}} @test_load_ss(
// IR: load float, ptr {{.*}}, align 1
// IR-NOT: call {{.*}} @_mm_load_ss

void test_store_ss(float *p, __m128 a) { _mm_store_ss(p, a); }
// IR-LABEL: define {{.*}} @test_store_ss(
// IR: store float {{.*}}, ptr {{.*}}, align 1
// IR-NOT: call {{.*}} @_mm_store_ss

__m128 test_sqrt_ss(__m128 a) { return _mm_sqrt_ss(a); }
// IR-LABEL: define {{.*}} @test_sqrt_ss(
// IR: call float @llvm.sqrt.f32
// IR-NOT: call {{.*}} @_mm_sqrt_ss

__m128d test_load_sd(double const *p) { return _mm_load_sd(p); }
void test_store_sd(double *p, __m128d a) { _mm_store_sd(p, a); }
__m128d test_sqrt_sd(__m128d a, __m128d b) { return _mm_sqrt_sd(a, b); }
__m128d test_setzero_pd() { return _mm_setzero_pd(); }

__m128i test_integer_ops(__m128i a, __m128i b) {
  a = _mm_cmpeq_epi8(a, b);
  a = _mm_cmpeq_epi16(a, b);
  a = _mm_or_si128(a, b);
  a = _mm_xor_si128(a, b);
  a = _mm_unpackhi_epi8(a, b);
  a = _mm_unpacklo_epi8(a, b);
  a = _mm_unpackhi_epi16(a, b);
  return _mm_unpacklo_epi16(a, b);
}

__m128i test_immediates(__m128i a) {
  a = _mm_shuffle_epi32(a, 27);
  a = _mm_slli_si128(a, 256);
  return _mm_srli_si128(a, 257);
}

__m128i test_loadu(__m128i_u const *p) { return _mm_loadu_si128(p); }
// IR-LABEL: define {{.*}} @test_loadu(
// IR: load <2 x i64>, ptr {{.*}}, align 1
// IR-NOT: call {{.*}} @_mm_loadu_si128

void test_storeu(__m128i_u *p, __m128i a) { _mm_storeu_si128(p, a); }
int test_movemask(__m128i a) { return _mm_movemask_epi8(a); }
__m128i test_cvtsi32(int a) { return _mm_cvtsi32_si128(a); }
__m128i test_setzero_si128() { return _mm_setzero_si128(); }

}

// IR-NOT: clang-msvc-required-target-features
