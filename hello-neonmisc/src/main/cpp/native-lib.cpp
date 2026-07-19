// Integration-level probe for the NEON "residue" instructions: vector FP-misc
// (FABS/FNEG/FSQRT in FP32 and FP64, FACGE/FACGT, FCVTL/FCVTN between FP32 and
// FP64, FRECPE/FRSQRTE estimates, SQABS/SQNEG saturating), byte-lane
// MUL/MLA/MLS and byte shift-accumulate/rounding shifts, scalar pairwise
// (ADDP-D, FADDP/FMAXP/FMINP), and scalar by-element FMUL/FMLA. Each family
// runs in a HOT LOOP past the JIT gear-up threshold so the heavy-tier
// lowerings are exercised. Exact ops check exact golden values; the
// architected estimate ops check the spec's relative-error bound. Any
// violation aborts() (SIGABRT).

#include <android/log.h>
#include <arm_neon.h>
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#define LOG_TAG "helloneonmisc"

namespace {

constexpr int kHotIters = 4000;  // > gear-up threshold (1000)

__attribute__((noinline)) bool CheckFpMisc32() {
  alignas(16) const float v[4] = {-2.25f, 4.0f, -0.5f, 9.0f};
  float32x4_t x = vld1q_f32(v);
  float a[4], n[4], s[4];
  vst1q_f32(a, vabsq_f32(x));
  vst1q_f32(n, vnegq_f32(x));
  vst1q_f32(s, vsqrtq_f32(vabsq_f32(x)));
  if (a[0] != 2.25f || a[1] != 4.0f || a[2] != 0.5f || a[3] != 9.0f) return false;
  if (n[0] != 2.25f || n[1] != -4.0f || n[2] != 0.5f || n[3] != -9.0f) return false;
  if (s[0] != 1.5f || s[1] != 2.0f || s[3] != 3.0f) return false;
  // FACGE/FACGT: |x| vs |y| masks.
  alignas(16) const float w[4] = {2.25f, -5.0f, 0.5f, -9.0f};
  uint32_t ge[4], gt[4];
  vst1q_u32(ge, vcageq_f32(x, vld1q_f32(w)));  // |x|>=|w|: {1,0,1,1}
  vst1q_u32(gt, vcagtq_f32(x, vld1q_f32(w)));  // |x|>|w| : {0,0,0,0}
  if (ge[0] != ~0u || ge[1] != 0 || ge[2] != ~0u || ge[3] != ~0u) return false;
  if (gt[0] != 0 || gt[1] != 0 || gt[2] != 0 || gt[3] != 0) return false;
  return true;
}

__attribute__((noinline)) bool CheckFpMisc64() {
  alignas(16) const double v[2] = {-6.25, 2.25};
  float64x2_t x = vld1q_f64(v);
  double a[2], n[2], s[2];
  vst1q_f64(a, vabsq_f64(x));
  vst1q_f64(n, vnegq_f64(x));
  vst1q_f64(s, vsqrtq_f64(vabsq_f64(x)));
  return a[0] == 6.25 && a[1] == 2.25 && n[0] == 6.25 && n[1] == -2.25 &&
         s[0] == 2.5 && s[1] == 1.5;
}

__attribute__((noinline)) bool CheckCvt() {
  // FCVTL: two FP32 -> two FP64; FCVTN back. Exact values survive round-trip.
  alignas(8) const float f[2] = {1.5f, -2048.25f};
  float64x2_t wide = vcvt_f64_f32(vld1_f32(f));
  double d[2];
  vst1q_f64(d, wide);
  if (d[0] != 1.5 || d[1] != -2048.25) return false;
  float narrow[2];
  vst1_f32(narrow, vcvt_f32_f64(wide));
  return narrow[0] == 1.5f && narrow[1] == -2048.25f;
}

__attribute__((noinline)) bool CheckEstimates() {
  alignas(16) const float v[4] = {0.5f, 2.0f, 7.0f, 123.0f};
  float32x4_t x = vld1q_f32(v);
  float re[4], rs[4];
  vst1q_f32(re, vrecpeq_f32(x));
  vst1q_f32(rs, vrsqrteq_f32(x));
  for (int i = 0; i < 4; i++) {
    // Architected estimates: relative error < 2^-8 vs 1/x and 1/sqrt(x).
    if (std::fabs(re[i] * v[i] - 1.0f) > 0.01f) return false;
    if (std::fabs(rs[i] * rs[i] * v[i] - 1.0f) > 0.02f) return false;
  }
  return true;
}

__attribute__((noinline)) bool CheckSaturating() {
  alignas(16) const int32_t v[4] = {INT32_MIN, -7, 0, INT32_MAX};
  int32x4_t x = vld1q_s32(v);
  int32_t qa[4], qn[4];
  vst1q_s32(qa, vqabsq_s32(x));
  vst1q_s32(qn, vqnegq_s32(x));
  return qa[0] == INT32_MAX && qa[1] == 7 && qa[2] == 0 && qa[3] == INT32_MAX &&
         qn[0] == INT32_MAX && qn[1] == 7 && qn[2] == 0 && qn[3] == -INT32_MAX;
}

__attribute__((noinline)) bool CheckByteLane() {
  alignas(16) const uint8_t a[16] = {20, 3, 5, 7, 11, 13, 100, 255,
                                     2, 4, 6, 8, 10, 12, 14, 16};
  alignas(16) const uint8_t b[16] = {13, 3, 5, 7, 9, 11, 3, 2,
                                     1, 2, 3, 4, 5, 6, 7, 8};
  uint8x16_t va = vld1q_u8(a), vb = vld1q_u8(b);
  uint8_t mul[16], mla[16];
  vst1q_u8(mul, vmulq_u8(va, vb));
  vst1q_u8(mla, vmlaq_u8(vdupq_n_u8(1), va, vb));
  // 20*13 = 260 -> 4 (mod 256); 100*3 = 300 -> 44; 255*2 = 510 -> 254.
  if (mul[0] != 4 || mul[6] != 44 || mul[7] != 254 || mul[15] != 128) return false;
  if (mla[0] != 5 || mla[6] != 45 || mla[7] != 255) return false;
  // Byte shifts: SSRA (arith shift-right accumulate) and URSHR (rounding).
  alignas(16) const int8_t sv[16] = {-128, -16, 8, 127, -1, 0, 33, -33,
                                     64, -64, 5, -5, 100, -100, 2, -2};
  int8_t ssra[16];
  vst1q_s8(ssra, vsraq_n_s8(vdupq_n_s8(1), vld1q_s8(sv), 3));
  // 1 + (v >> 3): -128>>3 = -16 -> -15; 127>>3 = 15 -> 16; -1>>3 = -1 -> 0.
  if (ssra[0] != -15 || ssra[3] != 16 || ssra[4] != 0) return false;
  alignas(16) const uint8_t uv[16] = {7, 8, 12, 255, 4, 20, 36, 129,
                                      1, 2, 3, 5, 9, 17, 65, 250};
  uint8_t urshr[16];
  vst1q_u8(urshr, vrshrq_n_u8(vld1q_u8(uv), 3));
  // round((v)/8): 7 -> 1, 8 -> 1, 12 -> 2 (7+4=11>>3=1? no: (12+4)>>3 = 2), 255 -> 32.
  if (urshr[0] != 1 || urshr[1] != 1 || urshr[2] != 2 || urshr[3] != 32) return false;
  // Byte insert shifts: SLI keeps the low `shift` bits of dst, SRI the high ones.
  alignas(16) const uint8_t src[16] = {0x15, 0xA8, 0x01, 0x80, 0xFF, 0x00, 0x42, 0x7E,
                                       0x15, 0xA8, 0x01, 0x80, 0xFF, 0x00, 0x42, 0x7E};
  uint8_t sli[16], sri[16];
  vst1q_u8(sli, vsliq_n_u8(vdupq_n_u8(0xFF), vld1q_u8(src), 3));
  // (src << 3) | (0xFF & 0x07): 0x15 -> 0xA8|0x07 = 0xAF; 0x80 -> 0x00|0x07 = 0x07.
  if (sli[0] != 0xAF || sli[3] != 0x07 || sli[4] != 0xFF) return false;
  vst1q_u8(sri, vsriq_n_u8(vdupq_n_u8(0xFF), vld1q_u8(src), 3));
  // (src >> 3) | (0xFF & 0xE0): 0xA8 -> 0x15|0xE0 = 0xF5; 0x01 -> 0x00|0xE0 = 0xE0.
  if (sri[1] != 0xF5 || sri[2] != 0xE0 || sri[4] != 0xFF) return false;
  // Signed rounding shifts: SRSHR floors after adding 1<<(shift-1) in wide
  // arithmetic; SRSRA/URSRA accumulate the rounded shift into dst.
  alignas(16) const int8_t rs[16] = {-1, -128, 7, 127, -4, 4, -100, 100,
                                     -1, -128, 7, 127, -4, 4, -100, 100};
  int8_t srshr[16], srsra[16];
  vst1q_s8(srshr, vrshrq_n_s8(vld1q_s8(rs), 3));
  // (-1+4)>>3 = 0; (-128+4)>>3 = -124>>3 = -16 (floor); (127+4)>>3 = 16.
  if (srshr[0] != 0 || srshr[1] != -16 || srshr[3] != 16) return false;
  vst1q_s8(srsra, vrsraq_n_s8(vdupq_n_s8(1), vld1q_s8(rs), 1));
  // 1 + ((-128+1)>>1) = 1 + (-127>>1) = 1 + (-64) = -63; 1 + ((127+1)>>1) = 65.
  if (srsra[1] != -63 || srsra[3] != 65) return false;
  uint8_t ursra[16];
  vst1q_u8(ursra, vrsraq_n_u8(vdupq_n_u8(1), vld1q_u8(uv), 3));
  // 1 + ((255+4)>>3) = 33 for lane 3 (uv[3]=255); 1 + ((7+4)>>3) = 2 for lane 0.
  if (ursra[0] != 2 || ursra[3] != 33) return false;
  return true;
}

__attribute__((noinline)) bool CheckScalarPairwise() {
  alignas(16) const uint64_t q[2] = {0x100000001ULL, 0x200000003ULL};
  if (vpaddd_u64(vld1q_u64(q)) != 0x300000004ULL) return false;  // addp d
  alignas(8) const float f[2] = {1.25f, 2.5f};
  float32x2_t v = vld1_f32(f);
  if (vpadds_f32(v) != 3.75f) return false;   // faddp s
  if (vpmaxs_f32(v) != 2.5f) return false;    // fmaxp s
  if (vpmins_f32(v) != 1.25f) return false;   // fminp s
  return true;
}

__attribute__((noinline)) bool CheckScalarByElement() {
  alignas(16) const float v[4] = {1.5f, -2.0f, 4.0f, 0.25f};
  float32x4_t q = vld1q_f32(v);
  if (vmuls_laneq_f32(8.0f, q, 3) != 2.0f) return false;       // fmul s, s, v[3]
  if (vfmas_laneq_f32(1.0f, 3.0f, q, 1) != -5.0f) return false;  // 1 + 3*-2
  return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloneonmisc_MainActivity_probeNeonmisc(JNIEnv* env, jobject /*this*/) {
  struct Case { const char* name; bool (*fn)(); };
  const Case cases[] = {
      {"fp-misc.4s", CheckFpMisc32},     {"fp-misc.2d", CheckFpMisc64},
      {"fcvtl/fcvtn", CheckCvt},         {"frecpe/frsqrte", CheckEstimates},
      {"sqabs/sqneg", CheckSaturating},  {"byte-lane", CheckByteLane},
      {"scalar-pairwise", CheckScalarPairwise},
      {"scalar-by-elem", CheckScalarByElement},
  };
  std::string report = "NEON residue probe (heavy-tier):\n";
  bool all_ok = true;
  for (const auto& c : cases) {
    bool ok = true;
    for (int i = 0; i < kHotIters; i++) {
      ok = c.fn() && ok;
    }
    char line[64];
    snprintf(line, sizeof(line), "  %-16s x%d: %s\n", c.name, kHotIters, ok ? "OK" : "FAIL");
    report += line;
    all_ok = all_ok && ok;
  }
  __android_log_print(all_ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, LOG_TAG, "%s",
                      report.c_str());
  if (!all_ok) abort();
  return env->NewStringUTF(report.c_str());
}
