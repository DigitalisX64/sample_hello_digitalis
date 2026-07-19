// Integration-level probe for the quantized-ML instruction set: ARMv8.6 I8MM
// matrix multiply (SMMLA/UMMLA/USMMLA), BF16 (BFDOT/BFMMLA/BFMLALB/BFMLALT),
// and the ARMv8.3 indexed FCMLA — via the <arm_neon.h> intrinsics. Each family
// runs in a HOT LOOP past the JIT gear-up threshold so the heavy-tier
// lowerings are exercised; results are checked against hand-computed exact
// golden values (integer-valued inputs keep every product exact in bf16/f32).
// Any mismatch aborts() (SIGABRT).

#include <android/log.h>
#include <arm_neon.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define LOG_TAG "helloi8mmbf16"

namespace {

constexpr int kHotIters = 4000;  // > gear-up threshold (1000)

// SMMLA: C(2x2) += A(2x8, rows of int8) * B(8x2, given as rows of Bt).
// A = row0 1..8, row1 9..16 ; B columns all-ones and 1..8.
__attribute__((noinline)) bool CheckSmmla() {
  const int8_t a_bytes[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  const int8_t b_bytes[16] = {1, 1, 1, 1, 1, 1, 1, 1,   // Bt row0 (= B col0): ones
                              1, 2, 3, 4, 5, 6, 7, 8};  // Bt row1 (= B col1)
  int8x16_t a = vld1q_s8(a_bytes), b = vld1q_s8(b_bytes);
  int32x4_t acc = vdupq_n_s32(10);
  int32x4_t r = vmmlaq_s32(acc, a, b);
  int32_t out[4];
  vst1q_s32(out, r);
  // row0.col0 = 36+10, row0.col1 = 1*1+..+8*8 = 204+10, row1.col0 = 100+10,
  // row1.col1 = 9*1+10*2+...+16*8 = 492+10.
  return out[0] == 46 && out[1] == 214 && out[2] == 110 && out[3] == 502;
}

__attribute__((noinline)) bool CheckUmmla() {
  const uint8_t a_bytes[16] = {200, 1, 1, 1, 1, 1, 1, 1, 100, 2, 2, 2, 2, 2, 2, 2};
  const uint8_t b_bytes[16] = {1, 1, 1, 1, 1, 1, 1, 1, 250, 1, 1, 1, 1, 1, 1, 1};
  uint8x16_t a = vld1q_u8(a_bytes), b = vld1q_u8(b_bytes);
  uint32x4_t r = vmmlaq_u32(vdupq_n_u32(0), a, b);
  uint32_t out[4];
  vst1q_u32(out, r);
  // r0c0 = 200+7 = 207 ; r0c1 = 200*250+7 = 50007 ; r1c0 = 100+14 = 114 ;
  // r1c1 = 100*250+14 = 25014.
  return out[0] == 207 && out[1] == 50007 && out[2] == 114 && out[3] == 25014;
}

__attribute__((noinline)) bool CheckUsmmla() {
  const uint8_t a_bytes[16] = {255, 0, 0, 0, 0, 0, 0, 0, 128, 1, 0, 0, 0, 0, 0, 0};
  const int8_t b_bytes[16] = {-1, 0, 0, 0, 0, 0, 0, 0, 2, -3, 0, 0, 0, 0, 0, 0};
  uint8x16_t a = vld1q_u8(a_bytes);
  int8x16_t b = vld1q_s8(b_bytes);
  int32x4_t r = vusmmlaq_s32(vdupq_n_s32(0), a, b);
  int32_t out[4];
  vst1q_s32(out, r);
  // r0c0 = 255*-1 = -255 ; r0c1 = 255*2 = 510 ; r1c0 = 128*-1 = -128 ;
  // r1c1 = 128*2 + 1*-3 = 253.  (A unsigned, B signed.)
  return out[0] == -255 && out[1] == 510 && out[2] == -128 && out[3] == 253;
}

bfloat16x8_t MakeBf16x8(const float (&v)[8]) {
  uint16_t half[8];
  for (int i = 0; i < 8; i++) {
    bfloat16_t h = vcvth_bf16_f32(v[i]);
    memcpy(&half[i], &h, 2);
  }
  bfloat16x8_t out;
  memcpy(&out, half, 16);
  return out;
}

__attribute__((noinline)) bool CheckBfdot() {
  const float av[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const float bv[8] = {2, 2, 2, 2, 3, 3, 3, 3};
  bfloat16x8_t a = MakeBf16x8(av), b = MakeBf16x8(bv);
  float32x4_t r = vbfdotq_f32(vdupq_n_f32(1.0f), a, b);
  float out[4];
  vst1q_f32(out, r);
  // lane j = 1 + a[2j]*b[2j] + a[2j+1]*b[2j+1]: 7, 15, 34, 46.
  return out[0] == 7.0f && out[1] == 15.0f && out[2] == 34.0f && out[3] == 46.0f;
}

__attribute__((noinline)) bool CheckBfmmla() {
  // C(2x2) += A(2x4) * B(4x2): A rows {1,2,3,4},{5,6,7,8}; Bt rows (= B cols)
  // {1,1,1,1},{2,2,2,2}.
  const float av[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const float bv[8] = {1, 1, 1, 1, 2, 2, 2, 2};
  bfloat16x8_t a = MakeBf16x8(av), b = MakeBf16x8(bv);
  float32x4_t r = vbfmmlaq_f32(vdupq_n_f32(0.0f), a, b);
  float out[4];
  vst1q_f32(out, r);
  // 10, 20, 26, 52.
  return out[0] == 10.0f && out[1] == 20.0f && out[2] == 26.0f && out[3] == 52.0f;
}

__attribute__((noinline)) bool CheckBfmlal() {
  const float av[8] = {1, 10, 2, 20, 3, 30, 4, 40};
  const float bv[8] = {2, 100, 2, 100, 2, 100, 2, 100};
  bfloat16x8_t a = MakeBf16x8(av), b = MakeBf16x8(bv);
  float32x4_t lo = vbfmlalbq_f32(vdupq_n_f32(0.5f), a, b);   // even lanes
  float32x4_t hi = vbfmlaltq_f32(vdupq_n_f32(-0.5f), a, b);  // odd lanes
  float lout[4], hout[4];
  vst1q_f32(lout, lo);
  vst1q_f32(hout, hi);
  // B: 0.5 + {2,4,6,8} ; T: -0.5 + {1000,2000,3000,4000}.
  return lout[0] == 2.5f && lout[1] == 4.5f && lout[2] == 6.5f && lout[3] == 8.5f &&
         hout[0] == 999.5f && hout[1] == 1999.5f && hout[2] == 2999.5f && hout[3] == 3999.5f;
}

// Indexed FCMLA, rotations 0 and 90, broadcasting complex pair 1 of b.
__attribute__((noinline)) bool CheckFcmlaIdx() {
  alignas(16) const float acc[4] = {1, 2, 3, 4};
  alignas(16) const float av[4] = {2, 3, 4, 5};    // pairs (2+3i), (4+5i)
  alignas(16) const float bv[4] = {9, 9, 6, 7};    // pair 1 = (6+7i)
  float32x4_t r = vld1q_f32(acc), a = vld1q_f32(av), b = vld1q_f32(bv);
  float32x4_t r0 = vcmlaq_laneq_f32(r, a, b, 1);        // rot0: += a.re * b
  float32x4_t r90 = vcmlaq_rot90_laneq_f32(r, a, b, 1); // rot90: (-a.im*b.im, a.im*b.re)
  float o0[4], o90[4];
  vst1q_f32(o0, r0);
  vst1q_f32(o90, r90);
  // rot0: {1+2*6, 2+2*7, 3+4*6, 4+4*7} = {13, 16, 27, 32}
  // rot90: {1-3*7, 2+3*6, 3-5*7, 4+5*6} = {-20, 20, -32, 34}
  return o0[0] == 13 && o0[1] == 16 && o0[2] == 27 && o0[3] == 32 &&
         o90[0] == -20 && o90[1] == 20 && o90[2] == -32 && o90[3] == 34;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloi8mmbf16_MainActivity_probeI8mmBf16(JNIEnv* env, jobject /*this*/) {
  struct Case { const char* name; bool (*fn)(); };
  const Case cases[] = {
      {"SMMLA", CheckSmmla},   {"UMMLA", CheckUmmla},   {"USMMLA", CheckUsmmla},
      {"BFDOT", CheckBfdot},   {"BFMMLA", CheckBfmmla}, {"BFMLALB/T", CheckBfmlal},
      {"FCMLA[idx]", CheckFcmlaIdx},
  };
  std::string report = "I8MM + BF16 + indexed-FCMLA probe (heavy-tier):\n";
  bool all_ok = true;
  for (const auto& c : cases) {
    bool ok = true;
    for (int i = 0; i < kHotIters; i++) {
      ok = c.fn() && ok;
    }
    char line[64];
    snprintf(line, sizeof(line), "  %-11s x%d: %s\n", c.name, kHotIters, ok ? "OK" : "FAIL");
    report += line;
    all_ok = all_ok && ok;
  }
  __android_log_print(all_ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, LOG_TAG, "%s",
                      report.c_str());
  if (!all_ok) abort();
  return env->NewStringUTF(report.c_str());
}
