// Integration-level probe for FCSEL (scalar floating-point conditional select,
// S and D) under translation, in the region shapes that once miscompiled at
// region level: the select's source register being re-read AFTER a not-taken
// select (the stale-forwarded-source shape), the NZCV flags staying live across
// the select for a LATER consumer, and multiple selects in one region. Each
// shape runs in a HOT LOOP past the JIT gear-up threshold so the heavy tier's
// lowering is exercised; every result is checked against exact golden values
// and any mismatch aborts() (SIGABRT) so the sample suite flags it.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#define LOG_TAG "hellofcsel"

namespace {

constexpr int kHotIters = 4000;  // > gear-up threshold (1000)

// fcmp a,b ; fadd t=x+y (FP ops leave NZCV untouched) ; fcsel r = gt ? x : y ;
// csinc c = gt ? 1 : 0 consuming the SAME flags after the select.
__attribute__((noinline)) void FcselFlagsLiveS(float a, float b, float x, float y,
                                               float* sel, float* sum, uint32_t* pred) {
  float r, t;
  uint32_t c;
  __asm__ volatile(
      "fcmp %s3, %s4\n\t"
      "fadd %s1, %s5, %s6\n\t"
      "fcsel %s0, %s5, %s6, gt\n\t"
      "csinc %w2, wzr, wzr, le\n\t"
      : "=&w"(r), "=&w"(t), "=&r"(c)
      : "w"(a), "w"(b), "w"(x), "w"(y));
  *sel = r;
  *sum = t;
  *pred = c;
}

// Not-taken select, then RE-READ the true-side source: the historical bug made
// the cached read of x observe the select result instead of x.
__attribute__((noinline)) void FcselSourceReuseS(float a, float b, float x, float y,
                                                 float* sel, float* reread) {
  float r, t;
  __asm__ volatile(
      "fcmp %s2, %s3\n\t"
      "fcsel %s0, %s4, %s5, gt\n\t"
      "fadd %s1, %s4, %s4\n\t"
      : "=&w"(r), "=&w"(t)
      : "w"(a), "w"(b), "w"(x), "w"(y));
  *sel = r;
  *reread = t;
}

__attribute__((noinline)) void FcselSourceReuseD(double a, double b, double x, double y,
                                                 double* sel, double* reread) {
  double r, t;
  __asm__ volatile(
      "fcmp %d2, %d3\n\t"
      "fcsel %d0, %d4, %d5, gt\n\t"
      "fadd %d1, %d4, %d4\n\t"
      : "=&w"(r), "=&w"(t)
      : "w"(a), "w"(b), "w"(x), "w"(y));
  *sel = r;
  *reread = t;
}

// Two selects with two distinct compares in one region.
__attribute__((noinline)) void FcselMultiS(float a, float b, float x, float y,
                                           float* r1, float* r2) {
  float s1, s2;
  __asm__ volatile(
      "fcmp %s2, %s3\n\t"
      "fcsel %s0, %s4, %s5, gt\n\t"
      "fcmp %s4, %s5\n\t"
      "fcsel %s1, %s2, %s3, lt\n\t"
      : "=&w"(s1), "=&w"(s2)
      : "w"(a), "w"(b), "w"(x), "w"(y));
  *r1 = s1;
  *r2 = s2;
}

__attribute__((noinline)) void FcselFlagsLiveD(double a, double b, double x, double y,
                                               double* sel, double* sum, uint32_t* pred) {
  double r, t;
  uint32_t c;
  __asm__ volatile(
      "fcmp %d3, %d4\n\t"
      "fadd %d1, %d5, %d6\n\t"
      "fcsel %d0, %d5, %d6, gt\n\t"
      "csinc %w2, wzr, wzr, le\n\t"
      : "=&w"(r), "=&w"(t), "=&r"(c)
      : "w"(a), "w"(b), "w"(x), "w"(y));
  *sel = r;
  *sum = t;
  *pred = c;
}

bool RunChecks() {
  for (int i = 0; i < kHotIters; i++) {
    const float fa = 1.0f + static_cast<float>(i & 3);   // 1..4
    const float fb = 2.5f;                               // taken iff fa > 2.5
    const float fx = 8.0f, fy = -4.0f;
    const bool gt = fa > fb;

    float sel, sum, reread;
    uint32_t pred;
    FcselFlagsLiveS(fa, fb, fx, fy, &sel, &sum, &pred);
    if (sel != (gt ? fx : fy) || sum != fx + fy || pred != (gt ? 1u : 0u)) return false;

    FcselSourceReuseS(1.0f, 2.0f, fx, fy, &sel, &reread);  // cond false: picks fy
    if (sel != fy || reread != fx + fx) return false;

    double dsel, dreread;
    FcselSourceReuseD(1.0, 2.0, 16.25, -3.5, &dsel, &dreread);
    if (dsel != -3.5 || dreread != 32.5) return false;

    float r1, r2;
    FcselMultiS(fa, fb, fx, fy, &r1, &r2);  // r1 = gt?fx:fy ; r2 = (fx<fy)?fa:fb
    if (r1 != (gt ? fx : fy) || r2 != fb) return false;

    double dsum;
    FcselFlagsLiveD(static_cast<double>(fa), 2.5, 9.75, -1.25, &dsel, &dsum, &pred);
    if (dsel != (gt ? 9.75 : -1.25) || dsum != 8.5 || pred != (gt ? 1u : 0u)) return false;
  }
  return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellofcsel_MainActivity_probeFcsel(JNIEnv* env, jobject /*this*/) {
  const bool ok = RunChecks();
  char buf[160];
  snprintf(buf, sizeof(buf),
           "FCSEL region-shape probe (S+D, flags-live, source-reuse, multi) x%d: %s\n",
           kHotIters, ok ? "OK" : "FAIL");
  __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, LOG_TAG, "%s", buf);
  if (!ok) abort();
  return env->NewStringUTF(buf);
}
