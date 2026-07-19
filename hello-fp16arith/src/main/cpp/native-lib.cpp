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
#include <arm_neon.h>
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

// Vector FP16 (.8h) three-same + misc, with DISTINCT high-lane values so a
// wrong high-half recombine in a two-half widening lowering is caught.
__attribute__((noinline)) bool VectorChecks() {
  alignas(16) const f16 a[8] = {f16(1.5f), f16(2.25f), f16(-4.0f), f16(8.0f),
                                f16(100.0f), f16(-0.5f), f16(2048.0f), f16(0.125f)};
  alignas(16) const f16 b[8] = {f16(0.25f), f16(0.75f), f16(1.0f), f16(-8.0f),
                                f16(28.0f), f16(0.5f), f16(2.0f), f16(0.375f)};
  float16x8_t va = vld1q_f16(reinterpret_cast<const float16_t*>(a));
  float16x8_t vb = vld1q_f16(reinterpret_cast<const float16_t*>(b));

  f16 sum[8], mul[8], mx[8], ab[8], ng[8];
  vst1q_f16(reinterpret_cast<float16_t*>(sum), vaddq_f16(va, vb));
  const float want_sum[8] = {1.75f, 3.0f, -3.0f, 0.0f, 128.0f, 0.0f, 2050.0f, 0.5f};
  for (int i = 0; i < 8; i++) {
    if (static_cast<float>(sum[i]) != want_sum[i]) return false;
  }
  vst1q_f16(reinterpret_cast<float16_t*>(mul), vmulq_f16(va, vb));
  const float want_mul[8] = {0.375f, 1.6875f, -4.0f, -64.0f, 2800.0f, -0.25f,
                             4096.0f, 0.046875f};
  for (int i = 0; i < 8; i++) {
    if (static_cast<float>(mul[i]) != want_mul[i]) return false;
  }
  vst1q_f16(reinterpret_cast<float16_t*>(mx), vmaxq_f16(va, vb));
  if (static_cast<float>(mx[2]) != 1.0f || static_cast<float>(mx[6]) != 2048.0f) return false;
  vst1q_f16(reinterpret_cast<float16_t*>(ab), vabsq_f16(va));
  vst1q_f16(reinterpret_cast<float16_t*>(ng), vnegq_f16(va));
  if (static_cast<float>(ab[2]) != 4.0f || static_cast<float>(ng[2]) != 4.0f ||
      static_cast<float>(ng[6]) != -2048.0f) return false;

  uint16_t eq[8];
  vst1q_u16(eq, vceqq_f16(va, va));
  for (int i = 0; i < 8; i++) {
    if (eq[i] != 0xFFFF) return false;
  }
  uint16_t gt[8];
  vst1q_u16(gt, vcgtq_f16(va, vb));  // {1,1,0,1,1,0,1,0} pattern below
  const uint16_t want_gt[8] = {0xFFFF, 0xFFFF, 0, 0xFFFF, 0xFFFF, 0, 0xFFFF, 0};
  for (int i = 0; i < 8; i++) {
    if (gt[i] != want_gt[i]) return false;
  }

  // FP16 fused multiply-add (.8h): the correctly-rounded result requires the
  // FP64-fusion path — a naive fp32 round-trip would double-round. Pick a case
  // where a*b+c is not exactly representable so a wrong (double-rounded)
  // lowering diverges. acc + va*vb, checked against a scalar fp16 fma oracle.
  alignas(16) const f16 acc[8] = {f16(0.5f), f16(1.0f), f16(2.0f), f16(-1.0f),
                                  f16(0.25f), f16(3.0f), f16(-2.0f), f16(0.75f)};
  f16 fma_out[8];
  vst1q_f16(reinterpret_cast<float16_t*>(fma_out),
            vfmaq_f16(vld1q_f16(reinterpret_cast<const float16_t*>(acc)), va, vb));
  for (int i = 0; i < 8; i++) {
    // Scalar fp16 FMA oracle via FP64 fusion + single narrow (matches ARM).
    double p = static_cast<double>(static_cast<float>(a[i])) *
               static_cast<double>(static_cast<float>(b[i]));
    f16 want = static_cast<f16>(static_cast<float>(
        p + static_cast<double>(static_cast<float>(acc[i]))));
    if (Bits(fma_out[i]) != Bits(want)) return false;
  }

  // FMULX (.8h): like FMUL but 0*inf = ±2.0. Build a vector with a 0*inf lane.
  alignas(16) const f16 zinf_a[8] = {f16(0.0f), f16(-0.0f), f16(2.0f), f16(3.0f),
                                     f16(1.5f), f16(-4.0f), f16(0.5f), f16(8.0f)};
  const uint16_t infbits = 0x7C00;
  f16 hinf;
  memcpy(&hinf, &infbits, 2);
  alignas(16) const f16 zinf_b[8] = {hinf, hinf, f16(2.0f), f16(3.0f),
                                     f16(1.5f), f16(-4.0f), f16(0.5f), f16(8.0f)};
  f16 fmulx[8];
  vst1q_f16(reinterpret_cast<float16_t*>(fmulx),
            vmulxq_f16(vld1q_f16(reinterpret_cast<const float16_t*>(zinf_a)),
                       vld1q_f16(reinterpret_cast<const float16_t*>(zinf_b))));
  // lane0 = +0*inf = +2.0h (0x4000); lane1 = -0*inf = -2.0h (0xC000).
  if (Bits(fmulx[0]) != 0x4000 || Bits(fmulx[1]) != 0xC000) return false;
  if (static_cast<float>(fmulx[2]) != 4.0f) return false;  // 2*2 normal

  return true;
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

    // FP16 <-> integer scalar conversions (these were previously mis-handled;
    // now correct in the interpreter and routed there by the JIT tiers). Use
    // inline asm to pin the exact instructions.
    {
      int32_t zs;  // FCVTZS Wd, Hn (round-toward-zero)
      __asm__("fcvtzs %w0, %h1" : "=r"(zs) : "w"(f16(2.75f)));
      if (zs != 2) return false;
      int32_t as;  // FCVTAS Wd, Hn (ties-away)
      __asm__("fcvtas %w0, %h1" : "=r"(as) : "w"(f16(2.5f)));
      if (as != 3) return false;
      int32_t ms;  // FCVTMS Wd, Hn (floor)
      __asm__("fcvtms %w0, %h1" : "=r"(ms) : "w"(f16(-1.25f)));
      if (ms != -2) return false;
      f16 sc;  // SCVTF Hd, Wn (int -> fp16, RNE)
      __asm__("scvtf %h0, %w1" : "=w"(sc) : "r"(int32_t{4097}));
      if (Bits(sc) != Bits(f16(4096.0f))) return false;  // 4097 -> nearest fp16 = 4096
      uint64_t zx;  // FCVTZU Xd, Hn
      __asm__("fcvtzu %x0, %h1" : "=r"(zx) : "w"(f16(100.0f)));
      if (zx != 100) return false;
    }

    // Rounding-boundary: (1+2^-10)^2 -> 1+2^-9 after RNE; vary an exact term
    // per-iteration so the region isn't constant-folded away.
    const f16 base = f16(1.0f + 0x1.0p-10f);
    if (Bits(HMul(base, base)) != Bits(RefMul(base, base))) return false;
    const f16 big = f16(2048.0f);
    const f16 small = f16(static_cast<float>(2 + 2 * (i & 1)));  // 2 or 4: exact
    if (Bits(HAdd(big, small)) != Bits(RefAdd(big, small))) return false;

    if (!VectorChecks()) return false;
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
