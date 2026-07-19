// Integration-level probe for the ARMv8.2 (FEAT_FP16) scalar half-precision
// instructions: FADD/FSUB/FMUL/FDIV/FNMUL, FSQRT, FMAXNM/FMINNM, FRINTA/FRINTZ,
// FCMP, FCVT (half<->single<->double) and FMOV (register + immediate), all in H
// form. _Float16 arithmetic with -march=armv8.2-a+fp16 emits them directly;
// inline asm pins the encodings for the ops C won't reliably emit. Runs in a
// HOT LOOP past the JIT gear-up threshold so the heavy tier's half-precision
// lowering is exercised. Golden values are exactly representable in FP16 (bit
// patterns hardcoded where fixed), plus a rounding-boundary case checked
// against the widen-compute-narrow reference computed through a different
// instruction path. Any mismatch aborts() (SIGABRT).

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define LOG_TAG "hellofp16arith"

namespace {

constexpr int kHotIters = 4000;  // > gear-up threshold (1000)

using f16 = _Float16;

uint16_t Bits(f16 v) {
  uint16_t b;
  memcpy(&b, &v, sizeof(b));
  return b;
}

__attribute__((noinline)) f16 HAdd(f16 a, f16 b) { return a + b; }
__attribute__((noinline)) f16 HSub(f16 a, f16 b) { return a - b; }
__attribute__((noinline)) f16 HMul(f16 a, f16 b) { return a * b; }
__attribute__((noinline)) f16 HDiv(f16 a, f16 b) { return a / b; }

__attribute__((noinline)) f16 HSqrt(f16 a) {
  f16 r;
  __asm__("fsqrt %h0, %h1" : "=w"(r) : "w"(a));
  return r;
}
__attribute__((noinline)) f16 HMaxnm(f16 a, f16 b) {
  f16 r;
  __asm__("fmaxnm %h0, %h1, %h2" : "=w"(r) : "w"(a), "w"(b));
  return r;
}
__attribute__((noinline)) f16 HMinnm(f16 a, f16 b) {
  f16 r;
  __asm__("fminnm %h0, %h1, %h2" : "=w"(r) : "w"(a), "w"(b));
  return r;
}
__attribute__((noinline)) f16 HRinta(f16 a) {
  f16 r;
  __asm__("frinta %h0, %h1" : "=w"(r) : "w"(a));
  return r;
}
__attribute__((noinline)) f16 HRintz(f16 a) {
  f16 r;
  __asm__("frintz %h0, %h1" : "=w"(r) : "w"(a));
  return r;
}
__attribute__((noinline)) f16 HNmul(f16 a, f16 b) {
  f16 r;
  __asm__("fnmul %h0, %h1, %h2" : "=w"(r) : "w"(a), "w"(b));
  return r;
}
__attribute__((noinline)) f16 HMovImm() {
  f16 r;
  __asm__("fmov %h0, #1.5" : "=w"(r));
  return r;
}
__attribute__((noinline)) bool HGt(f16 a, f16 b) { return a > b; }  // fcmp h
__attribute__((noinline)) float HToS(f16 a) { return static_cast<float>(a); }
__attribute__((noinline)) double HToD(f16 a) { return static_cast<double>(a); }
__attribute__((noinline)) f16 SToH(float a) { return static_cast<f16>(a); }
__attribute__((noinline)) f16 DToH(double a) { return static_cast<f16>(a); }

// Reference via explicit widen -> float compute -> narrow: a DIFFERENT
// instruction path (fcvt s,h ; fadd s ; fcvt h,s) that equals the correctly
// rounded FP16 result for +,-,*,/ (FP32 mantissa > 2*11+2).
__attribute__((noinline)) f16 RefAdd(f16 a, f16 b) {
  volatile float wa = static_cast<float>(a), wb = static_cast<float>(b);
  return static_cast<f16>(wa + wb);
}
__attribute__((noinline)) f16 RefMul(f16 a, f16 b) {
  volatile float wa = static_cast<float>(a), wb = static_cast<float>(b);
  return static_cast<f16>(wa * wb);
}

bool RunChecks() {
  for (int i = 0; i < kHotIters; i++) {
    // Exact-representable cases (fixed bit patterns).
    if (Bits(HAdd(f16(1.5f), f16(2.25f))) != 0x4380) return false;   // 3.75h
    if (Bits(HSub(f16(4.0f), f16(1.5f))) != 0x4100) return false;    // 2.5h
    if (Bits(HMul(f16(0.5f), f16(8.0f))) != 0x4400) return false;    // 4.0h
    if (Bits(HDiv(f16(10.0f), f16(4.0f))) != 0x4100) return false;   // 2.5h
    if (Bits(HSqrt(f16(2.25f))) != 0x3E00) return false;             // 1.5h
    if (Bits(HNmul(f16(2.0f), f16(1.5f))) != 0xC200) return false;   // -3.0h
    if (Bits(HMaxnm(f16(3.0f), f16(-5.0f))) != 0x4200) return false; // 3.0h
    if (Bits(HMinnm(f16(3.0f), f16(-5.0f))) != 0xC500) return false; // -5.0h
    if (Bits(HRinta(f16(2.5f))) != 0x4200) return false;             // 3.0h (away)
    if (Bits(HRintz(f16(2.75f))) != 0x4000) return false;            // 2.0h
    if (Bits(HMovImm()) != 0x3E00) return false;                     // 1.5h

    if (!HGt(f16(2.5f), f16(1.0f)) || HGt(f16(-1.0f), f16(0.0f))) return false;

    if (HToS(f16(6.25f)) != 6.25f || HToD(f16(-0.5f)) != -0.5) return false;
    if (Bits(SToH(1023.5f)) != 0x63FF) return false;  // 1023.5h exact
    if (Bits(DToH(-2048.0)) != 0xE800) return false;  // -2048h exact

    // Rounding-boundary: (1+2^-10)^2 -> 1+2^-9 after RNE; vary an exact term
    // per-iteration so the region isn't constant-folded away.
    const f16 base = f16(1.0f + 0x1.0p-10f);
    if (Bits(HMul(base, base)) != Bits(RefMul(base, base))) return false;
    const f16 big = f16(2048.0f);
    const f16 small = f16(static_cast<float>(2 + 2 * (i & 1)));  // 2 or 4: exact
    if (Bits(HAdd(big, small)) != Bits(RefAdd(big, small))) return false;
  }
  return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellofp16arith_MainActivity_probeFp16arith(JNIEnv* env, jobject /*this*/) {
  const bool ok = RunChecks();
  char buf[192];
  snprintf(buf, sizeof(buf),
           "scalar FP16 probe (arith, sqrt, minmax, rint, cmp, cvt, fmov-imm) x%d: %s\n",
           kHotIters, ok ? "OK" : "FAIL");
  __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, LOG_TAG, "%s", buf);
  if (!ok) abort();
  return env->NewStringUTF(buf);
}
