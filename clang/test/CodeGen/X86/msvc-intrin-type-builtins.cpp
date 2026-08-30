// RUN: %clang_cc1 -triple x86_64-pc-windows-msvc -fms-extensions \
// RUN:   -fms-compatibility -fsyntax-only -ast-dump %s | \
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

typedef union __declspec(intrin_type) __declspec(align(32)) __m256i {
  char i8[32];
  short i16[16];
  int i32[8];
  long long i64[4];
} __m256i;

extern "C" {
int _rdrand16_step(unsigned short *);
int _rdrand32_step(unsigned int *);
int _rdseed16_step(unsigned short *);
int _rdseed32_step(unsigned int *);

float _mm_cvtss_f32(__m128);
__m128 _mm_div_ps(__m128, __m128);
__m128 _mm_load_ss(float const *);
__m128 _mm_set_ps1(float);
__m128 _mm_set_ss(float);
__m128 _mm_sqrt_ss(__m128);
void _mm_store_ss(float *, __m128);

__m128 _mm_cvtpd_ps(__m128d);
double _mm_cvtsd_f64(__m128d);
__m128d _mm_cvtss_sd(__m128d, __m128);
__m128d _mm_load_sd(double const *);
__m128d _mm_set_sd(double);
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

__m256i _mm256_cmpeq_epi16(__m256i, __m256i);
__m256i _mm256_cmpeq_epi8(__m256i, __m256i);
int _mm256_movemask_epi8(__m256i);
__m256i _mm256_setzero_si256(void);
void _mm256_zeroupper(void);
}

#pragma intrinsic(_rdrand16_step)
#pragma intrinsic(_rdrand32_step)
#pragma intrinsic(_rdseed16_step)
#pragma intrinsic(_rdseed32_step)

// AST: FunctionDecl {{.*}} _mm_cvtss_f32
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_div_ps
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_load_ss
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_set_ps1
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_cvtpd_ps
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_sqrt_sd
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm_xor_si128
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm256_cmpeq_epi8
// AST: BuiltinAttr {{.*}} Implicit
// AST: FunctionDecl {{.*}} _mm256_zeroupper
// AST: BuiltinAttr {{.*}} Implicit

extern "C" {

int test_rdrand16(unsigned short *p) { return _rdrand16_step(p); }
int test_rdrand32(unsigned int *p) { return _rdrand32_step(p); }
int test_rdseed16(unsigned short *p) { return _rdseed16_step(p); }
int test_rdseed32(unsigned int *p) { return _rdseed32_step(p); }

// IR-LABEL: define {{.*}} @test_rdrand32(
// IR: call { i32, i32 } @llvm.x86.rdrand.32()
// IR-NOT: call {{.*}} @_rdrand32_step
// IR-LABEL: define {{.*}} @test_rdseed32(
// IR: call { i32, i32 } @llvm.x86.rdseed.32()
// IR-NOT: call {{.*}} @_rdseed32_step

float test_cvtss_f32(__m128 a) { return _mm_cvtss_f32(a); }
// IR-LABEL: define {{.*}} float @test_cvtss_f32(
// IR: extractelement <4 x float>
// IR-NOT: call {{.*}} @_mm_cvtss_f32

__m128 test_div_ps(__m128 a, __m128 b) { return _mm_div_ps(a, b); }
// IR-LABEL: define {{.*}} @test_div_ps(
// IR: fdiv <4 x float>
// IR-NOT: call {{.*}} @_mm_div_ps

__m128 test_set_ss(float a) { return _mm_set_ss(a); }
// IR-LABEL: define {{.*}} @test_set_ss(
// IR: insertelement <4 x float>
// IR-NOT: call {{.*}} @_mm_set_ss

__m128 test_set_ps1(float a) { return _mm_set_ps1(a); }
// IR-LABEL: define {{.*}} @test_set_ps1(
// IR: shufflevector <4 x float>
// IR-NOT: call {{.*}} @_mm_set_ps1

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

__m128 test_cvtpd_ps(__m128d a) { return _mm_cvtpd_ps(a); }
// IR-LABEL: define {{.*}} @test_cvtpd_ps(
// IR: call <4 x float> @llvm.x86.sse2.cvtpd2ps
// IR-NOT: call {{.*}} @_mm_cvtpd_ps

double test_cvtsd_f64(__m128d a) { return _mm_cvtsd_f64(a); }
// IR-LABEL: define {{.*}} double @test_cvtsd_f64(
// IR: extractelement <2 x double>
// IR-NOT: call {{.*}} @_mm_cvtsd_f64

__m128d test_cvtss_sd(__m128d a, __m128 b) { return _mm_cvtss_sd(a, b); }
// IR-LABEL: define {{.*}} @test_cvtss_sd(
// IR: fpext float {{.*}} to double
// IR-NOT: call {{.*}} @_mm_cvtss_sd

__m128d test_set_sd(double a) { return _mm_set_sd(a); }
// IR-LABEL: define {{.*}} @test_set_sd(
// IR: insertelement <2 x double>
// IR-NOT: call {{.*}} @_mm_set_sd

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

__m256i test_mm256_cmpeq_epi8(__m256i a, __m256i b) {
  return _mm256_cmpeq_epi8(a, b);
}

__m256i test_mm256_cmpeq_epi16(__m256i a, __m256i b) {
  return _mm256_cmpeq_epi16(a, b);
}

int test_mm256_movemask_epi8(__m256i a) {
  return _mm256_movemask_epi8(a);
}

__m256i test_mm256_setzero_si256() { return _mm256_setzero_si256(); }

void test_mm256_zeroupper() { _mm256_zeroupper(); }

static __forceinline int wrapped_mm256_ops(__m256i a, __m256i b) {
  return _mm256_movemask_epi8(_mm256_cmpeq_epi8(a, b));
}

__declspec(noinline) int test_wrapped_mm256_ops(__m256i a, __m256i b) {
  return wrapped_mm256_ops(a, b);
}
// IR-LABEL: define {{.*}} @test_wrapped_mm256_ops(
// IR-SAME: #[[AVX2:[0-9]+]] {
// IR: icmp eq <32 x i8>
// IR: call{{.*}} i32 @llvm.x86.avx2.pmovmskb

}

// IR-NOT: clang-msvc-required-target-features
// IR: attributes #[[AVX2]] = {{.*}}"target-features"="{{[^"]*}}+avx2{{[^"]*}}"
