// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -target-feature +mmx -target-feature +sse -emit-llvm -o - %s | FileCheck %s

typedef float v4sf __attribute__((vector_size(16)));
typedef int v2si __attribute__((vector_size(8)));
typedef short v4hi __attribute__((vector_size(8)));
typedef char v8qi __attribute__((vector_size(8)));
typedef long long v1di __attribute__((vector_size(8)));

v2si test_cvtps2pi(v4sf x) {
  // CHECK-LABEL: @test_cvtps2pi(
  // CHECK: call x86_mmx @llvm.x86.sse.cvtps2pi(<4 x float>
  return __builtin_ia32_cvtps2pi(x);
}

v2si test_cvttps2pi(v4sf x) {
  // CHECK-LABEL: @test_cvttps2pi(
  // CHECK: call x86_mmx @llvm.x86.sse.cvttps2pi(<4 x float>
  return __builtin_ia32_cvttps2pi(x);
}

v4sf test_cvtpi2ps(v4sf x, v2si y) {
  // CHECK-LABEL: @test_cvtpi2ps(
  // CHECK: call <4 x float> @llvm.x86.sse.cvtpi2ps(<4 x float>
  return __builtin_ia32_cvtpi2ps(x, y);
}

v4hi test_pmaxsw(v4hi x, v4hi y) {
  // CHECK-LABEL: @test_pmaxsw(
  // CHECK: call x86_mmx @llvm.x86.mmx.pmaxs.w(
  return __builtin_ia32_pmaxsw(x, y);
}

v8qi test_pmaxub(v8qi x, v8qi y) {
  // CHECK-LABEL: @test_pmaxub(
  // CHECK: call x86_mmx @llvm.x86.mmx.pmaxu.b(
  return __builtin_ia32_pmaxub(x, y);
}

v4hi test_pminsw(v4hi x, v4hi y) {
  // CHECK-LABEL: @test_pminsw(
  // CHECK: call x86_mmx @llvm.x86.mmx.pmins.w(
  return __builtin_ia32_pminsw(x, y);
}

v8qi test_pminub(v8qi x, v8qi y) {
  // CHECK-LABEL: @test_pminub(
  // CHECK: call x86_mmx @llvm.x86.mmx.pminu.b(
  return __builtin_ia32_pminub(x, y);
}

void test_movntq(v1di *p, v1di x) {
  // CHECK-LABEL: @test_movntq(
  // CHECK: call void @llvm.x86.mmx.movnt.dq(ptr {{.*}}, x86_mmx
  __builtin_ia32_movntq(p, x);
}
