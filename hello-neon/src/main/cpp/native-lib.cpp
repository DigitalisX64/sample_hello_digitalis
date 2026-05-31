// Broad ARM NEON intrinsic survey -- one short verifier per intrinsic
// family. Each probe runs the intrinsic against a portable C reference
// (or against a hand-computed expected value) and surfaces any
// divergence as a FAIL line on screen.
//
// Coverage groups:
//   1.  Load / store (vld1, vst1, vld2/3/4, vst2/3/4)
//   2.  Integer arith at every common lane width (.8B/.16B/.4H/.8H/.2S/.4S/.2D)
//   3.  Saturating arith (vqaddq, vqsubq, vqdmulhq)
//   4.  Compare (vceqq, vcgtq, vcgeq, vcltq, vcleq)
//   5.  Min / max / abs / neg
//   6.  Shifts (vshlq, vshlq_n, vshrq_n, vrshrq_n, vqshlq, vshrn_n)
//   7.  Logical (vandq, vorrq, veorq, vbicq, vornq, vmvnq)
//   8.  Bit manipulation (vcntq, vclzq, vbslq, vrbitq)
//   9.  Reduce / across-lane (vaddvq, vmaxvq, vminvq)
//   10. Pairwise (vpaddq, vpaddlq, vpmaxq, vpminq)
//   11. Permute (vrev*, vextq, vzip1q/2q, vuzp1q/2q, vtrn1q/2q, vqtbl1q)
//   12. Convert (s32<->f32, narrow vqmovn, widen vmovl)
//   13. Lane access (vget/setq_lane, vdupq_n, vdupq_laneq)
//   14. FP arith (vaddq_f32/.2D, vsubq, vmulq, vmlaq, vmlsq, vdivq_f32,
//       vsqrtq, vrecpeq, vrsqrteq, vabdq) -- subsumes hello-fp-vector
//   15. Polynomial multiply (vmull_p8) -- PMULL via +crypto

#include <arm_acle.h>
#include <arm_neon.h>
#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "helloneon"

namespace {

template <typename T, int N>
bool array_eq(const T (&a)[N], const T (&b)[N]) {
  for (int i = 0; i < N; ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

bool fp_eq(float a, float b, float eps = 1e-4f) {
  float d = a - b;
  return (d < 0 ? -d : d) < eps;
}
bool fp_eq(double a, double b, double eps = 1e-8) {
  double d = a - b;
  return (d < 0 ? -d : d) < eps;
}

void emit(std::string* log, const char* label, bool ok) {
  char buf[64];
  snprintf(buf, sizeof(buf), "  %-22s %s\n", label, ok ? "OK" : "FAIL");
  *log += buf;
}

// Helper used by probes to bail early at the first failing sub-test, with a
// trailing tag that tells the caller which intrinsic family member broke.
#define CHECK(cond, tag)                                            \
  do {                                                              \
    if (!(cond)) {                                                  \
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,               \
                          "%s FAIL at %s", __func__, tag);          \
      return false;                                                 \
    }                                                               \
  } while (0)

//==================================================================
// 1. Load / Store
//==================================================================
bool probe_loads_stores() {
  alignas(16) uint8_t src[16];
  alignas(16) uint8_t dst[16] = {};
  for (int i = 0; i < 16; ++i) src[i] = static_cast<uint8_t>(i + 1);
  uint8x16_t v = vld1q_u8(src);
  vst1q_u8(dst, v);
  if (memcmp(src, dst, 16) != 0) return false;

  // vld2q_u8 — deinterleaves a 32-byte run into even / odd lanes.
  alignas(16) uint8_t inter2[32];
  for (int i = 0; i < 32; ++i) inter2[i] = static_cast<uint8_t>(i);
  uint8x16x2_t v2 = vld2q_u8(inter2);
  alignas(16) uint8_t out20[16], out21[16];
  vst1q_u8(out20, v2.val[0]);
  vst1q_u8(out21, v2.val[1]);
  for (int i = 0; i < 16; ++i) {
    if (out20[i] != 2 * i || out21[i] != 2 * i + 1) return false;
  }

  // vld3q_u8
  alignas(16) uint8_t inter3[48];
  for (int i = 0; i < 48; ++i) inter3[i] = static_cast<uint8_t>(i);
  uint8x16x3_t v3 = vld3q_u8(inter3);
  alignas(16) uint8_t out30[16], out31[16], out32[16];
  vst1q_u8(out30, v3.val[0]);
  vst1q_u8(out31, v3.val[1]);
  vst1q_u8(out32, v3.val[2]);
  for (int i = 0; i < 16; ++i) {
    if (out30[i] != 3 * i || out31[i] != 3 * i + 1 ||
        out32[i] != 3 * i + 2)
      return false;
  }

  // vld4q_u8
  alignas(16) uint8_t inter4[64];
  for (int i = 0; i < 64; ++i) inter4[i] = static_cast<uint8_t>(i);
  uint8x16x4_t v4 = vld4q_u8(inter4);
  alignas(16) uint8_t out40[16], out41[16], out42[16], out43[16];
  vst1q_u8(out40, v4.val[0]);
  vst1q_u8(out41, v4.val[1]);
  vst1q_u8(out42, v4.val[2]);
  vst1q_u8(out43, v4.val[3]);
  for (int i = 0; i < 16; ++i) {
    if (out40[i] != 4 * i + 0 || out41[i] != 4 * i + 1 ||
        out42[i] != 4 * i + 2 || out43[i] != 4 * i + 3)
      return false;
  }

  // vst2q_u8 — interleave back.
  alignas(16) uint8_t reinter[32] = {};
  vst2q_u8(reinter, v2);
  if (memcmp(reinter, inter2, 32) != 0) return false;
  return true;
}

//==================================================================
// 2. Integer arithmetic across lane widths
//==================================================================
bool probe_int_arith() {
  // .16B add (lane width 8 bits)
  {
    alignas(16) uint8_t a[16], b[16], got[16], want[16];
    for (int i = 0; i < 16; ++i) {
      a[i] = static_cast<uint8_t>(i + 1);
      b[i] = static_cast<uint8_t>(2 * i);
      want[i] = static_cast<uint8_t>(a[i] + b[i]);
    }
    vst1q_u8(got, vaddq_u8(vld1q_u8(a), vld1q_u8(b)));
    if (!array_eq(got, want)) return false;
  }
  // .8H sub (lane width 16 bits) — signed
  {
    alignas(16) int16_t a[8] = {-1000, -10, 0, 1, 100, 1000, 10000, 30000};
    alignas(16) int16_t b[8] = {1, -1, 2, -2, 3, -3, 4, -4};
    alignas(16) int16_t got[8], want[8];
    for (int i = 0; i < 8; ++i) want[i] = a[i] - b[i];
    vst1q_s16(got, vsubq_s16(vld1q_s16(a), vld1q_s16(b)));
    if (!array_eq(got, want)) return false;
  }
  // .4S mul (lane width 32 bits)
  {
    alignas(16) uint32_t a[4] = {1, 2, 3, 4};
    alignas(16) uint32_t b[4] = {10, 100, 1000, 10000};
    alignas(16) uint32_t got[4], want[4] = {10, 200, 3000, 40000};
    vst1q_u32(got, vmulq_u32(vld1q_u32(a), vld1q_u32(b)));
    if (!array_eq(got, want)) return false;
  }
  // .2D add (lane width 64 bits) — uses ADD Vd.2D, Vn.2D, Vm.2D
  {
    alignas(16) uint64_t a[2] = {0x1000000000000000ULL, 0x123ULL};
    alignas(16) uint64_t b[2] = {0xfeULL, 0x100000000ULL};
    alignas(16) uint64_t got[2], want[2] = {a[0] + b[0], a[1] + b[1]};
    vst1q_u64(got, vaddq_u64(vld1q_u64(a), vld1q_u64(b)));
    if (!array_eq(got, want)) return false;
  }
  // ADDHN/SUBHN/RADDHN/RSUBHN .2S<-.2D: high 32 bits of the 64-bit add/sub
  // (R-variants add 2^31 first). Exercises the 64->32 narrowing high-half path.
  {
    uint64_t a[2] = {0x123456789ABCDEF0ULL, 0xFFFFFFFF80000000ULL};
    uint64_t b[2] = {0x0000000180000000ULL, 0x0000000100000000ULL};
    uint32_t got[2];
    vst1_u32(got, vaddhn_u64(vld1q_u64(a), vld1q_u64(b)));
    for (int i = 0; i < 2; ++i)
      CHECK(got[i] == static_cast<uint32_t>((a[i] + b[i]) >> 32), "vaddhn_u64");
    vst1_u32(got, vsubhn_u64(vld1q_u64(a), vld1q_u64(b)));
    for (int i = 0; i < 2; ++i)
      CHECK(got[i] == static_cast<uint32_t>((a[i] - b[i]) >> 32), "vsubhn_u64");
    vst1_u32(got, vraddhn_u64(vld1q_u64(a), vld1q_u64(b)));
    for (int i = 0; i < 2; ++i)
      CHECK(got[i] == static_cast<uint32_t>((a[i] + b[i] + 0x80000000ULL) >> 32),
            "vraddhn_u64");
    vst1_u32(got, vrsubhn_u64(vld1q_u64(a), vld1q_u64(b)));
    for (int i = 0; i < 2; ++i)
      CHECK(got[i] == static_cast<uint32_t>((a[i] - b[i] + 0x80000000ULL) >> 32),
            "vrsubhn_u64");
  }
  return true;
}

//==================================================================
// 3. Saturating arithmetic
//==================================================================
bool probe_saturating() {
  // vqaddq_u8 — unsigned saturating add, clamps to 0xff.
  {
    alignas(16) uint8_t a[16] = {200, 200, 50, 50, 0, 1, 254, 255,
                                 100, 200, 30, 20, 1, 1, 1, 1};
    alignas(16) uint8_t b[16] = {100, 60, 100, 200, 1, 255, 1, 1,
                                 56, 56, 226, 235, 0, 1, 2, 3};
    alignas(16) uint8_t got[16];
    vst1q_u8(got, vqaddq_u8(vld1q_u8(a), vld1q_u8(b)));
    for (int i = 0; i < 16; ++i) {
      uint32_t want32 = static_cast<uint32_t>(a[i]) + b[i];
      uint8_t want = want32 > 0xff ? 0xff : static_cast<uint8_t>(want32);
      CHECK(got[i] == want, "vqaddq_u8");
    }
  }
  // vqsubq_s16 — signed saturating sub
  {
    alignas(16) int16_t a[8] = {-32768, -32768, 0, 32767, 100, -100, 0, 1};
    alignas(16) int16_t b[8] = {1, 32767, 0, -1, 50, 50, 0, 1};
    alignas(16) int16_t got[8];
    vst1q_s16(got, vqsubq_s16(vld1q_s16(a), vld1q_s16(b)));
    for (int i = 0; i < 8; ++i) {
      int32_t want32 = static_cast<int32_t>(a[i]) - b[i];
      int16_t want = want32 < -32768  ? -32768
                     : want32 > 32767 ? 32767
                                      : static_cast<int16_t>(want32);
      CHECK(got[i] == want, "vqsubq_s16");
    }
  }
  // vqdmull_s16 — signed doubling widening multiply: 4x int16 -> 4x int32,
  // result = sat(2 * a * b). INT16_MIN squared doubles to 2^31 -> INT32_MAX.
  {
    alignas(16) int16_t a[4] = {3, 5, -32768, 7};
    alignas(16) int16_t b[4] = {4, 6, -32768, 2};
    alignas(16) int32_t got[4];
    vst1q_s32(got, vqdmull_s16(vld1_s16(a), vld1_s16(b)));
    for (int i = 0; i < 4; ++i) {
      int64_t prod = 2LL * a[i] * b[i];
      int32_t want = prod > 2147483647LL    ? 2147483647
                     : prod < -2147483648LL ? (-2147483647 - 1)
                                            : static_cast<int32_t>(prod);
      CHECK(got[i] == want, "vqdmull_s16");
    }
  }
  // vqdmlal_s16 / vqdmlsl_s16 — saturating doubling multiply-accumulate/subtract:
  // acc +/- sat(2*b*c), with the final accumulate also 32-bit saturating.
  {
    alignas(16) int32_t acc[4] = {100, -50, 100, -2147483640};
    alignas(16) int16_t b[4] = {3, 5, -32768, 7};
    alignas(16) int16_t c[4] = {4, 6, -32768, 2};
    alignas(16) int32_t lal[4], lsl[4];
    vst1q_s32(lal, vqdmlal_s16(vld1q_s32(acc), vld1_s16(b), vld1_s16(c)));
    vst1q_s32(lsl, vqdmlsl_s16(vld1q_s32(acc), vld1_s16(b), vld1_s16(c)));
    for (int i = 0; i < 4; ++i) {
      int64_t p = 2LL * b[i] * c[i];
      int32_t prod = p > 2147483647LL    ? 2147483647
                     : p < -2147483648LL ? (-2147483647 - 1)
                                         : static_cast<int32_t>(p);
      auto sat = [](int64_t v) -> int32_t {
        return v > 2147483647LL    ? 2147483647
               : v < -2147483648LL ? (-2147483647 - 1)
                                   : static_cast<int32_t>(v);
      };
      CHECK(lal[i] == sat(static_cast<int64_t>(acc[i]) + prod), "vqdmlal_s16");
      CHECK(lsl[i] == sat(static_cast<int64_t>(acc[i]) - prod), "vqdmlsl_s16");
    }
  }
  // vqdmull_s32 / vqdmlal_s32 / vqdmlsl_s32 — .2S->.2D doubling multiply with
  // 64-bit saturation, including INT32_MIN^2 doubling and 64-bit accumulate
  // overflow.
  {
    alignas(16) int32_t a[2] = {3, -2147483647 - 1};   // {3, INT32_MIN}
    alignas(16) int32_t b[2] = {5, -2147483647 - 1};   // {5, INT32_MIN}
    alignas(16) int64_t acc[2] = {100, 100};
    alignas(16) int64_t pr[2], lal[2], lsl[2];
    vst1q_s64(pr, vqdmull_s32(vld1_s32(a), vld1_s32(b)));
    vst1q_s64(lal, vqdmlal_s32(vld1q_s64(acc), vld1_s32(a), vld1_s32(b)));
    vst1q_s64(lsl, vqdmlsl_s32(vld1q_s64(acc), vld1_s32(a), vld1_s32(b)));
    auto sat = [](__int128 v) -> int64_t {
      __int128 mx = (static_cast<__int128>(1) << 63) - 1;
      __int128 mn = -(static_cast<__int128>(1) << 63);
      return static_cast<int64_t>(v > mx ? mx : (v < mn ? mn : v));
    };
    for (int i = 0; i < 2; ++i) {
      int64_t prod = sat(2 * static_cast<__int128>(a[i]) * b[i]);
      CHECK(pr[i] == prod, "vqdmull_s32");
      CHECK(lal[i] == sat(static_cast<__int128>(acc[i]) + prod), "vqdmlal_s32");
      CHECK(lsl[i] == sat(static_cast<__int128>(acc[i]) - prod), "vqdmlsl_s32");
    }
  }
  // vuqaddq_s8 — SUQADD: signed saturating accumulate of an unsigned addend.
  {
    alignas(16) int8_t a[16] = {100, -100, 10, 0, 127, -128, 50, -50,
                                1, 2, 3, -1, -2, -3, 64, -64};
    alignas(16) uint8_t b[16] = {50, 30, 5, 0, 10, 200, 100, 20,
                                 5, 255, 2, 1, 128, 3, 70, 16};
    alignas(16) int8_t got[16];
    vst1q_s8(got, vuqaddq_s8(vld1q_s8(a), vld1q_u8(b)));
    for (int i = 0; i < 16; ++i) {
      int32_t sum = static_cast<int32_t>(a[i]) + b[i];
      int8_t want = sum > 127 ? 127 : sum < -128 ? -128 : static_cast<int8_t>(sum);
      CHECK(got[i] == want, "vuqaddq_s8");
    }
  }
  // vsqaddq_u16 — USQADD: unsigned saturating accumulate of a signed addend.
  {
    alignas(16) uint16_t a[8] = {100, 0, 65535, 32768, 5, 10, 60000, 1};
    alignas(16) int16_t b[8] = {50, -10, 1, -1, -10, 32767, 10000, -1};
    alignas(16) uint16_t got[8];
    vst1q_u16(got, vsqaddq_u16(vld1q_u16(a), vld1q_s16(b)));
    for (int i = 0; i < 8; ++i) {
      int32_t sum = static_cast<int32_t>(a[i]) + b[i];
      uint16_t want = sum < 0 ? 0 : sum > 65535 ? 65535 : static_cast<uint16_t>(sum);
      CHECK(got[i] == want, "vsqaddq_u16");
    }
  }
  // .2D->.2S saturating extracts (64->32): SQXTN (s64->s32), UQXTN (u64->u32),
  // SQXTUN (s64->u32). Exercise in-range, overflow, and negative lanes.
  {
    int64_t s[2] = {0x100000000LL, -5};  // +overflow / negative
    int32_t sn[2];
    vst1_s32(sn, vqmovn_s64(vld1q_s64(s)));
    CHECK(sn[0] == 0x7FFFFFFF, "vqmovn_s64_pos");
    CHECK(sn[1] == -5, "vqmovn_s64_neg");

    uint64_t u[2] = {0x100000000ULL, 0xABCDu};  // overflow / in-range
    uint32_t un[2];
    vst1_u32(un, vqmovn_u64(vld1q_u64(u)));
    CHECK(un[0] == 0xFFFFFFFFu, "vqmovn_u64_ovf");
    CHECK(un[1] == 0xABCDu, "vqmovn_u64_inrange");

    int64_t su[2] = {-1, 0x1FFFFFFFFLL};  // negative -> 0 / overflow -> UINT32_MAX
    uint32_t sun[2];
    vst1_u32(sun, vqmovun_s64(vld1q_s64(su)));
    CHECK(sun[0] == 0u, "vqmovun_s64_neg");
    CHECK(sun[1] == 0xFFFFFFFFu, "vqmovun_s64_ovf");
  }
  return true;
}

//==================================================================
// 4. Compare
//==================================================================
bool probe_compare() {
  alignas(16) int32_t a[4] = {1, 5, 5, 8};
  alignas(16) int32_t b[4] = {2, 5, 4, 8};
  alignas(16) uint32_t got[4];

  // vceqq_s32 — equal -> 0xffffffff per lane, else 0.
  vst1q_u32(got, vceqq_s32(vld1q_s32(a), vld1q_s32(b)));
  uint32_t want_eq[4] = {0, 0xffffffffu, 0, 0xffffffffu};
  if (memcmp(got, want_eq, sizeof(got)) != 0) return false;

  // vcgtq_s32 — a > b
  vst1q_u32(got, vcgtq_s32(vld1q_s32(a), vld1q_s32(b)));
  uint32_t want_gt[4] = {0, 0, 0xffffffffu, 0};
  if (memcmp(got, want_gt, sizeof(got)) != 0) return false;

  // vcgeq_s32 — a >= b
  vst1q_u32(got, vcgeq_s32(vld1q_s32(a), vld1q_s32(b)));
  uint32_t want_ge[4] = {0, 0xffffffffu, 0xffffffffu, 0xffffffffu};
  if (memcmp(got, want_ge, sizeof(got)) != 0) return false;

  // vcltq_s32 — a < b
  vst1q_u32(got, vcltq_s32(vld1q_s32(a), vld1q_s32(b)));
  uint32_t want_lt[4] = {0xffffffffu, 0, 0, 0};
  if (memcmp(got, want_lt, sizeof(got)) != 0) return false;

  // vcleq_s32 — a <= b
  vst1q_u32(got, vcleq_s32(vld1q_s32(a), vld1q_s32(b)));
  uint32_t want_le[4] = {0xffffffffu, 0xffffffffu, 0, 0xffffffffu};
  if (memcmp(got, want_le, sizeof(got)) != 0) return false;

  return true;
}

//==================================================================
// 5. Min / Max / Abs / Neg
//==================================================================
bool probe_minmax_absneg() {
  alignas(16) int16_t a[8] = {-5, 10, 7, -8, 0, 32767, -32768, 100};
  alignas(16) int16_t b[8] = {3, 9, 7, -8, 1, 1, 1, -200};
  alignas(16) int16_t got[8], want[8];

  for (int i = 0; i < 8; ++i) want[i] = a[i] > b[i] ? a[i] : b[i];
  vst1q_s16(got, vmaxq_s16(vld1q_s16(a), vld1q_s16(b)));
  if (!array_eq(got, want)) return false;

  for (int i = 0; i < 8; ++i) want[i] = a[i] < b[i] ? a[i] : b[i];
  vst1q_s16(got, vminq_s16(vld1q_s16(a), vld1q_s16(b)));
  if (!array_eq(got, want)) return false;

  // vabsq_s16 — note that abs(INT16_MIN) overflows to INT16_MIN; matches HW.
  for (int i = 0; i < 8; ++i)
    want[i] = static_cast<int16_t>(a[i] < 0 ? -a[i] : a[i]);
  vst1q_s16(got, vabsq_s16(vld1q_s16(a)));
  if (!array_eq(got, want)) return false;

  for (int i = 0; i < 8; ++i) want[i] = static_cast<int16_t>(-a[i]);
  vst1q_s16(got, vnegq_s16(vld1q_s16(a)));
  if (!array_eq(got, want)) return false;
  return true;
}

//==================================================================
// 6. Shifts
//==================================================================
bool probe_shifts() {
  // vshlq_n_u32 (immediate shift left)
  {
    alignas(16) uint32_t a[4] = {1, 2, 3, 0xf0000000u};
    alignas(16) uint32_t got[4], want[4] = {1u << 4, 2u << 4, 3u << 4,
                                            0xf0000000u << 4};
    vst1q_u32(got, vshlq_n_u32(vld1q_u32(a), 4));
    if (!array_eq(got, want)) return false;
  }
  // vshrq_n_u32 (immediate shift right, logical)
  {
    alignas(16) uint32_t a[4] = {0x80000000u, 0xffffffffu, 256, 1};
    alignas(16) uint32_t got[4], want[4] = {0x80000000u >> 4, 0xffffffffu >> 4,
                                            256 >> 4, 1 >> 4};
    vst1q_u32(got, vshrq_n_u32(vld1q_u32(a), 4));
    if (!array_eq(got, want)) return false;
  }
  // vshlq_s16 (vector-by-vector shift; negative count -> right shift)
  {
    alignas(16) int16_t v[8] = {1, 1, 1, 1, 256, 256, 256, 256};
    alignas(16) int16_t sh[8] = {0, 1, 4, 15, -1, -4, -8, -15};
    alignas(16) int16_t got[8];
    vst1q_s16(got, vshlq_s16(vld1q_s16(v), vld1q_s16(sh)));
    int16_t want[8];
    for (int i = 0; i < 8; ++i) {
      if (sh[i] >= 0)
        want[i] = static_cast<int16_t>(static_cast<int32_t>(v[i]) << sh[i]);
      else
        want[i] = static_cast<int16_t>(v[i] >> -sh[i]);
    }
    if (!array_eq(got, want)) return false;
  }
  // vshll_n — shift left long by element size (SHLL): each lane widened and
  // shifted left by the source element width, landing in the high half.
  {
    alignas(16) uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    alignas(16) uint16_t got[8];
    vst1q_u16(got, vshll_n_u8(vld1_u8(a), 8));
    for (int i = 0; i < 8; ++i) {
      CHECK(got[i] == static_cast<uint16_t>(a[i]) << 8, "vshll_n_u8");
    }
    alignas(16) uint16_t b[4] = {0x0001, 0x0002, 0x0003, 0x0004};
    alignas(16) uint32_t got32[4];
    vst1q_u32(got32, vshll_n_u16(vld1_u16(b), 16));
    for (int i = 0; i < 4; ++i) {
      CHECK(got32[i] == static_cast<uint32_t>(b[i]) << 16, "vshll_n_u16");
    }
  }
  return true;
}

//==================================================================
// 7. Logical bitwise
//==================================================================
bool probe_logical() {
  alignas(16) uint32_t a[4] = {0xff00ff00u, 0xaaaaaaaau, 0u, 0xffffffffu};
  alignas(16) uint32_t b[4] = {0x00ff00ffu, 0x55555555u, 0xdeadbeefu,
                               0xcafebabeu};
  alignas(16) uint32_t got[4], want[4];

  for (int i = 0; i < 4; ++i) want[i] = a[i] & b[i];
  vst1q_u32(got, vandq_u32(vld1q_u32(a), vld1q_u32(b)));
  if (!array_eq(got, want)) return false;

  for (int i = 0; i < 4; ++i) want[i] = a[i] | b[i];
  vst1q_u32(got, vorrq_u32(vld1q_u32(a), vld1q_u32(b)));
  if (!array_eq(got, want)) return false;

  for (int i = 0; i < 4; ++i) want[i] = a[i] ^ b[i];
  vst1q_u32(got, veorq_u32(vld1q_u32(a), vld1q_u32(b)));
  if (!array_eq(got, want)) return false;

  // vbicq: a AND NOT b
  for (int i = 0; i < 4; ++i) want[i] = a[i] & ~b[i];
  vst1q_u32(got, vbicq_u32(vld1q_u32(a), vld1q_u32(b)));
  if (!array_eq(got, want)) return false;

  // vornq: a OR NOT b
  for (int i = 0; i < 4; ++i) want[i] = a[i] | ~b[i];
  vst1q_u32(got, vornq_u32(vld1q_u32(a), vld1q_u32(b)));
  if (!array_eq(got, want)) return false;

  for (int i = 0; i < 4; ++i) want[i] = ~a[i];
  vst1q_u32(got, vmvnq_u32(vld1q_u32(a)));
  if (!array_eq(got, want)) return false;

  // ORR/BIC (vector, immediate) are read-modify-write — distinct from the
  // register forms above and from MOVI/MVNI (replace). The immediate encodings
  // are forced via inline asm; the compiler does not reliably emit them.
  {
    alignas(16) uint32_t in[4] = {0xaaaaaaaau, 0x55555555u, 0xffff0000u,
                                  0x0000ffffu};
    alignas(16) uint32_t out[4];
    uint32x4_t v = vld1q_u32(in);
    asm("orr %0.4s, #0x0f" : "+w"(v));  // per 32-bit lane |= 0x0000000F
    vst1q_u32(out, v);
    for (int i = 0; i < 4; ++i)
      CHECK(out[i] == (in[i] | 0x0000000Fu), "orr_imm_4s");

    v = vld1q_u32(in);
    asm("bic %0.4s, #0x0f" : "+w"(v));  // per 32-bit lane &= ~0x0000000F
    vst1q_u32(out, v);
    for (int i = 0; i < 4; ++i)
      CHECK(out[i] == (in[i] & ~0x0000000Fu), "bic_imm_4s");
  }
  return true;
}

//==================================================================
// 8. Bit manipulation (cnt / clz / bsl / rbit)
//==================================================================
bool probe_bitmanip() {
  // vcntq_u8 — population count per byte
  {
    alignas(16) uint8_t a[16] = {0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f,
                                 0xff, 0xaa, 0x55, 0x80, 0x81, 0xc3, 0xe7, 0xfe};
    alignas(16) uint8_t got[16], want[16];
    for (int i = 0; i < 16; ++i) {
      uint8_t v = a[i];
      uint8_t c = 0;
      while (v) {
        c += v & 1;
        v >>= 1;
      }
      want[i] = c;
    }
    vst1q_u8(got, vcntq_u8(vld1q_u8(a)));
    if (!array_eq(got, want)) return false;
  }
  // vclzq_u32 — count leading zeros per 32-bit lane
  {
    alignas(16) uint32_t a[4] = {0, 1, 0x80000000u, 0x00f00000u};
    alignas(16) uint32_t got[4], want[4] = {32, 31, 0, 8};
    vst1q_u32(got, vclzq_u32(vld1q_u32(a)));
    if (!array_eq(got, want)) return false;
  }
  // vbslq_u32 — bit-select: (mask AND a) OR (~mask AND b)
  {
    alignas(16) uint32_t mask[4] = {0xff00ff00u, 0x0000ffffu, 0xffffffffu, 0u};
    alignas(16) uint32_t a[4] = {0xaaaaaaaau, 0xaaaaaaaau, 0xaaaaaaaau,
                                 0xaaaaaaaau};
    alignas(16) uint32_t b[4] = {0x55555555u, 0x55555555u, 0x55555555u,
                                 0x55555555u};
    alignas(16) uint32_t got[4], want[4];
    for (int i = 0; i < 4; ++i)
      want[i] = (mask[i] & a[i]) | (~mask[i] & b[i]);
    vst1q_u32(got, vbslq_u32(vld1q_u32(mask), vld1q_u32(a), vld1q_u32(b)));
    if (!array_eq(got, want)) return false;
  }
  // vrbitq_u8 — bit reverse within each byte
  {
    alignas(16) uint8_t a[16] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
                                 0xaa, 0x55, 0x00, 0xff, 0xc3, 0x3c, 0xf0, 0x0f};
    alignas(16) uint8_t got[16], want[16];
    for (int i = 0; i < 16; ++i) {
      uint8_t v = a[i], r = 0;
      for (int b = 0; b < 8; ++b) {
        r = static_cast<uint8_t>((r << 1) | (v & 1));
        v >>= 1;
      }
      want[i] = r;
    }
    vst1q_u8(got, vrbitq_u8(vld1q_u8(a)));
    if (!array_eq(got, want)) return false;
  }
  return true;
}

//==================================================================
// 9. Reduce (across-lane)
//==================================================================
bool probe_reduce() {
  alignas(16) int16_t v[8] = {1, -2, 3, -4, 5, -6, 7, -8};
  int16x8_t qv = vld1q_s16(v);

  // vaddvq_s16 — sum of all 8 lanes
  if (vaddvq_s16(qv) != -4) return false;

  // vmaxvq_s16
  if (vmaxvq_s16(qv) != 7) return false;

  // vminvq_s16
  if (vminvq_s16(qv) != -8) return false;

  // vaddvq_u32 with 32-bit width
  alignas(16) uint32_t w[4] = {1u, 2u, 3u, 4u};
  if (vaddvq_u32(vld1q_u32(w)) != 10u) return false;
  return true;
}

//==================================================================
// 10. Pairwise
//==================================================================
bool probe_pairwise() {
  // vpaddq_s16 — pairwise add across two source vectors
  {
    alignas(16) int16_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    alignas(16) int16_t b[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    alignas(16) int16_t got[8];
    // Layout of vpaddq: { a[0]+a[1], a[2]+a[3], a[4]+a[5], a[6]+a[7],
    //                     b[0]+b[1], b[2]+b[3], b[4]+b[5], b[6]+b[7] }
    int16_t want[8] = {3, 7, 11, 15, 30, 70, 110, 150};
    vst1q_s16(got, vpaddq_s16(vld1q_s16(a), vld1q_s16(b)));
    if (!array_eq(got, want)) return false;
  }
  // vpaddlq_s16 — pairwise widening add inside one source
  {
    alignas(16) int16_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    alignas(16) int32_t got[4], want[4] = {3, 7, 11, 15};
    vst1q_s32(got, vpaddlq_s16(vld1q_s16(a)));
    if (!array_eq(got, want)) return false;
  }
  // vpmaxq_u32
  {
    alignas(16) uint32_t a[4] = {1, 5, 3, 4};
    alignas(16) uint32_t b[4] = {10, 2, 30, 9};
    alignas(16) uint32_t got[4], want[4] = {5, 4, 10, 30};
    vst1q_u32(got, vpmaxq_u32(vld1q_u32(a), vld1q_u32(b)));
    if (!array_eq(got, want)) return false;
  }
  return true;
}

//==================================================================
// 11. Permute
//==================================================================
bool probe_permute() {
  alignas(16) uint32_t a[4] = {1, 2, 3, 4};
  alignas(16) uint32_t b[4] = {10, 20, 30, 40};

  // vrev64q_u32 — reverse 32-bit lanes within each 64-bit doubleword
  {
    alignas(16) uint32_t got[4], want[4] = {2, 1, 4, 3};
    vst1q_u32(got, vrev64q_u32(vld1q_u32(a)));
    CHECK(array_eq(got, want), "vrev64q_u32");
  }
  // vextq_u32 imm=2 — concatenate {a, b} and pull 4 lanes starting at lane 2.
  {
    alignas(16) uint32_t got[4], want[4] = {3, 4, 10, 20};
    vst1q_u32(got, vextq_u32(vld1q_u32(a), vld1q_u32(b), 2));
    CHECK(array_eq(got, want), "vextq_u32");
  }
  // vzip1q / vzip2q
  {
    alignas(16) uint32_t got[4], w1[4] = {1, 10, 2, 20}, w2[4] = {3, 30, 4, 40};
    vst1q_u32(got, vzip1q_u32(vld1q_u32(a), vld1q_u32(b)));
    CHECK(array_eq(got, w1), "vzip1q_u32");
    vst1q_u32(got, vzip2q_u32(vld1q_u32(a), vld1q_u32(b)));
    CHECK(array_eq(got, w2), "vzip2q_u32");
  }
  // vuzp1q / vuzp2q
  {
    alignas(16) uint32_t got[4], w1[4] = {1, 3, 10, 30}, w2[4] = {2, 4, 20, 40};
    vst1q_u32(got, vuzp1q_u32(vld1q_u32(a), vld1q_u32(b)));
    CHECK(array_eq(got, w1), "vuzp1q_u32");
    vst1q_u32(got, vuzp2q_u32(vld1q_u32(a), vld1q_u32(b)));
    CHECK(array_eq(got, w2), "vuzp2q_u32");
  }
  // vtrn1q / vtrn2q
  {
    alignas(16) uint32_t got[4], w1[4] = {1, 10, 3, 30}, w2[4] = {2, 20, 4, 40};
    vst1q_u32(got, vtrn1q_u32(vld1q_u32(a), vld1q_u32(b)));
    CHECK(array_eq(got, w1), "vtrn1q_u32");
    vst1q_u32(got, vtrn2q_u32(vld1q_u32(a), vld1q_u32(b)));
    CHECK(array_eq(got, w2), "vtrn2q_u32");
  }
  // vqtbl1q_u8 — byte-wise table lookup
  {
    alignas(16) uint8_t table[16];
    for (int i = 0; i < 16; ++i) table[i] = static_cast<uint8_t>(i * 7 + 1);
    alignas(16) uint8_t idx[16] = {15, 14, 13, 12, 11, 10, 9, 8,
                                   7, 6, 5, 4, 3, 2, 1, 0};
    alignas(16) uint8_t got[16];
    vst1q_u8(got, vqtbl1q_u8(vld1q_u8(table), vld1q_u8(idx)));
    for (int i = 0; i < 16; ++i)
      CHECK(got[i] == table[idx[i]], "vqtbl1q_u8");
  }
  return true;
}

//==================================================================
// 12. Conversion
//==================================================================
bool probe_convert() {
  // s32 -> f32 then f32 -> s32 round-trip (rounded toward zero on _s32).
  {
    alignas(16) int32_t a[4] = {-7, 0, 1, 1000};
    alignas(16) int32_t got[4];
    float32x4_t f = vcvtq_f32_s32(vld1q_s32(a));
    vst1q_s32(got, vcvtq_s32_f32(f));
    for (int i = 0; i < 4; ++i)
      if (got[i] != a[i]) return false;
  }
  // vmovl_u8 — widen low 8 lanes of .8B to 8 lanes of .8H
  {
    alignas(16) uint8_t a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0};
    alignas(16) uint16_t got[8], want[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    vst1q_u16(got, vmovl_u8(vld1_u8(a)));
    if (!array_eq(got, want)) return false;
  }
  // vqmovn_u16 — narrow with unsigned saturation
  {
    alignas(16) uint16_t a[8] = {0, 1, 255, 256, 1000, 32767, 65535, 100};
    alignas(8) uint8_t got[8];
    uint8_t want[8] = {0, 1, 255, 255, 255, 255, 255, 100};
    vst1_u8(got, vqmovn_u16(vld1q_u16(a)));
    if (memcmp(got, want, 8) != 0) return false;
  }
  return true;
}

//==================================================================
// 13. Lane access
//==================================================================
bool probe_lanes() {
  alignas(16) uint32_t a[4] = {0x11, 0x22, 0x33, 0x44};
  uint32x4_t v = vld1q_u32(a);

  if (vgetq_lane_u32(v, 0) != 0x11) return false;
  if (vgetq_lane_u32(v, 3) != 0x44) return false;

  v = vsetq_lane_u32(0xdeadbeef, v, 2);
  if (vgetq_lane_u32(v, 2) != 0xdeadbeef) return false;

  // vdupq_n
  uint32x4_t vdup = vdupq_n_u32(0xfeed);
  alignas(16) uint32_t got[4];
  vst1q_u32(got, vdup);
  for (int i = 0; i < 4; ++i)
    if (got[i] != 0xfeed) return false;

  // vdupq_laneq_u32 — broadcast lane 1 of v across the result.
  uint32x4_t vlane = vdupq_laneq_u32(v, 1);
  vst1q_u32(got, vlane);
  for (int i = 0; i < 4; ++i)
    if (got[i] != 0x22) return false;
  return true;
}

//==================================================================
// 14. FP arithmetic (subsumes hello-fp-vector)
//==================================================================
bool probe_fp_arith() {
  // .4S three-same family: FADD, FSUB, FMUL, FMLA, FMLS
  {
    alignas(16) float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    alignas(16) float b[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    alignas(16) float c[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    alignas(16) float out[4];
    float32x4_t va = vld1q_f32(a), vb = vld1q_f32(b), vc = vld1q_f32(c);

    vst1q_f32(out, vaddq_f32(va, vb));
    CHECK(fp_eq(out[0], 6) && fp_eq(out[1], 8) &&
          fp_eq(out[2], 10) && fp_eq(out[3], 12), "vaddq_f32");

    vst1q_f32(out, vsubq_f32(vb, va));
    for (int i = 0; i < 4; ++i) CHECK(fp_eq(out[i], 4.0f), "vsubq_f32");

    vst1q_f32(out, vmulq_f32(va, vb));
    float wmul[4] = {5, 12, 21, 32};
    for (int i = 0; i < 4; ++i) CHECK(fp_eq(out[i], wmul[i]), "vmulq_f32");

    {
      // Capture inputs into local stack vars so we can read them after the call.
      alignas(16) float a_in[4], b_in[4], c_in[4];
      vst1q_f32(a_in, va); vst1q_f32(b_in, vb); vst1q_f32(c_in, vc);
      vst1q_f32(out, vmlaq_f32(vc, va, vb));  // c + a*b
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                          "fmla a=%f,%f,%f,%f b=%f,%f,%f,%f c=%f,%f,%f,%f got=%f,%f,%f,%f",
                          a_in[0], a_in[1], a_in[2], a_in[3],
                          b_in[0], b_in[1], b_in[2], b_in[3],
                          c_in[0], c_in[1], c_in[2], c_in[3],
                          out[0], out[1], out[2], out[3]);
    }
    float wfma[4] = {5.5f, 12.5f, 21.5f, 32.5f};
    for (int i = 0; i < 4; ++i) CHECK(fp_eq(out[i], wfma[i]), "vmlaq_f32");

    vst1q_f32(out, vmlsq_f32(vc, va, vb));  // c - a*b
    float wfms[4] = {-4.5f, -11.5f, -20.5f, -31.5f};
    for (int i = 0; i < 4; ++i) CHECK(fp_eq(out[i], wfms[i]), "vmlsq_f32");
  }
  // .2D three-same family: FADD, FSUB, FMUL
  {
    alignas(16) double a[2] = {1.5, 2.5};
    alignas(16) double b[2] = {4.0, 8.0};
    alignas(16) double out[2];
    float64x2_t va = vld1q_f64(a), vb = vld1q_f64(b);

    vst1q_f64(out, vaddq_f64(va, vb));
    CHECK(fp_eq(out[0], 5.5) && fp_eq(out[1], 10.5), "vaddq_f64");

    vst1q_f64(out, vsubq_f64(vb, va));
    CHECK(fp_eq(out[0], 2.5) && fp_eq(out[1], 5.5), "vsubq_f64");

    vst1q_f64(out, vmulq_f64(va, vb));
    CHECK(fp_eq(out[0], 6.0) && fp_eq(out[1], 20.0), "vmulq_f64");
  }
  // vdivq_f32, vsqrtq_f32, vabdq_f32 (absolute difference)
  {
    alignas(16) float a[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    alignas(16) float b[4] = {2.0f, 4.0f, 5.0f, 8.0f};
    alignas(16) float out[4];
    vst1q_f32(out, vdivq_f32(vld1q_f32(a), vld1q_f32(b)));
    float wdiv[4] = {5.0f, 5.0f, 6.0f, 5.0f};
    for (int i = 0; i < 4; ++i) CHECK(fp_eq(out[i], wdiv[i]), "vdivq_f32");

    alignas(16) float sq[4] = {1.0f, 4.0f, 9.0f, 16.0f};
    vst1q_f32(out, vsqrtq_f32(vld1q_f32(sq)));
    float wsq[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    for (int i = 0; i < 4; ++i) CHECK(fp_eq(out[i], wsq[i]), "vsqrtq_f32");

    vst1q_f32(out, vabdq_f32(vld1q_f32(a), vld1q_f32(b)));
    float wabd[4] = {8.0f, 16.0f, 25.0f, 32.0f};
    for (int i = 0; i < 4; ++i) CHECK(fp_eq(out[i], wabd[i]), "vabdq_f32");
  }
  // Reciprocal estimate / reciprocal-sqrt estimate -- limited precision per
  // spec (~8 bits of mantissa), so the tolerance is loose.
  {
    alignas(16) float a[4] = {1.0f, 2.0f, 4.0f, 10.0f};
    alignas(16) float out[4];
    vst1q_f32(out, vrecpeq_f32(vld1q_f32(a)));
    for (int i = 0; i < 4; ++i) {
      float want = 1.0f / a[i];
      CHECK(fp_eq(out[i], want, 0.01f), "vrecpeq_f32");
    }
    vst1q_f32(out, vrsqrteq_f32(vld1q_f32(a)));
    for (int i = 0; i < 4; ++i) {
      float want = 1.0f / __builtin_sqrtf(a[i]);
      CHECK(fp_eq(out[i], want, 0.01f), "vrsqrteq_f32");
    }
  }
  return true;
}

//==================================================================
// 15. Polynomial multiply (PMULL via +crypto)
//==================================================================
bool probe_polymul() {
  // vmull_p8 — 8-bit polynomial multiply, widening to 16-bit.
  // Reference: GF(2)[x] multiply (no carry).
  alignas(16) uint8_t a[8] = {0x53, 0xff, 0x01, 0x80, 0x00, 0xaa, 0xcc, 0x11};
  alignas(16) uint8_t b[8] = {0xca, 0x80, 0x02, 0x80, 0xff, 0x55, 0x33, 0x10};
  poly8x8_t va = vreinterpret_p8_u8(vld1_u8(a));
  poly8x8_t vb = vreinterpret_p8_u8(vld1_u8(b));
  poly16x8_t r = vmull_p8(va, vb);
  alignas(16) uint16_t got[8];
  vst1q_u16(got, vreinterpretq_u16_p16(r));
  for (int i = 0; i < 8; ++i) {
    uint16_t want = 0;
    for (int j = 0; j < 8; ++j) {
      if (b[i] & (1u << j)) want ^= static_cast<uint16_t>(a[i]) << j;
    }
    if (got[i] != want) return false;
  }
  // vmull_high_p8 — PMULL2: same poly8 multiply on the upper 8 bytes of a
  // 16-byte vector.
  alignas(16) uint8_t a16[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                 0x53, 0xff, 0x01, 0x80, 0x07, 0xaa, 0xcc, 0x11};
  alignas(16) uint8_t b16[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                 0xca, 0x80, 0x02, 0x80, 0x07, 0x55, 0x33, 0x10};
  poly16x8_t rh = vmull_high_p8(vreinterpretq_p8_u8(vld1q_u8(a16)),
                                vreinterpretq_p8_u8(vld1q_u8(b16)));
  alignas(16) uint16_t goth[8];
  vst1q_u16(goth, vreinterpretq_u16_p16(rh));
  for (int i = 0; i < 8; ++i) {
    uint16_t want = 0;
    for (int j = 0; j < 8; ++j) {
      if (b16[i + 8] & (1u << j)) want ^= static_cast<uint16_t>(a16[i + 8]) << j;
    }
    if (goth[i] != want) return false;
  }
  return true;
}

//==================================================================
// 16. Unsigned integer reciprocal / reciprocal-sqrt estimate (URECPE/URSQRTE)
//==================================================================
static uint32_t ref_urecpe(uint32_t a) {
  if ((a & 0x80000000u) == 0) return 0xFFFFFFFFu;
  int in = static_cast<int>((a >> 23) & 0x1FF);
  int b = (1 << 19) / (in * 2 + 1);
  return static_cast<uint32_t>((b + 1) / 2) << 23;
}
static uint32_t ref_ursqrte(uint32_t a) {
  if ((a & 0xC0000000u) == 0) return 0xFFFFFFFFu;
  int in = static_cast<int>((a >> 23) & 0x1FF);
  int aa = (in < 256) ? (in * 2 + 1) : (((in >> 1) << 1) + 1) * 2;
  int b = 512;
  while (static_cast<int64_t>(aa) * (b + 1) * (b + 1) < (1 << 28)) b += 1;
  return static_cast<uint32_t>((b + 1) / 2) << 23;
}
bool probe_recip_estimate() {
  alignas(16) uint32_t in[4] = {0x40000000u, 0x80000000u, 0xC0000000u,
                                0x7FFFFFFFu};
  alignas(16) uint32_t got[4];
  vst1q_u32(got, vrecpeq_u32(vld1q_u32(in)));
  for (int i = 0; i < 4; ++i) CHECK(got[i] == ref_urecpe(in[i]), "vrecpeq_u32");
  vst1q_u32(got, vrsqrteq_u32(vld1q_u32(in)));
  for (int i = 0; i < 4; ++i) CHECK(got[i] == ref_ursqrte(in[i]), "vrsqrteq_u32");
  return true;
}

//==================================================================
// 17. CRC32C (Castagnoli) — ARMv8 CRC32C* ops via __crc32c* intrinsics.
//==================================================================
bool probe_crc32c() {
  auto crc32c_ref = [](uint32_t crc, const uint8_t* data, int n) {
    for (int i = 0; i < n; ++i) {
      crc ^= data[i];
      for (int k = 0; k < 8; ++k)
        crc = (crc >> 1) ^ ((crc & 1) ? 0x82F63B78u : 0u);
    }
    return crc;
  };
  uint32_t acc = 0xFFFFFFFFu;
  uint8_t b = 0xAB;
  CHECK(__crc32cb(acc, b) == crc32c_ref(acc, &b, 1), "__crc32cb");
  uint16_t h = 0xBEEF;
  uint8_t hb[2] = {0xEF, 0xBE};
  CHECK(__crc32ch(acc, h) == crc32c_ref(acc, hb, 2), "__crc32ch");
  uint32_t w = 0xCAFEBABEu;
  uint8_t wb[4] = {0xBE, 0xBA, 0xFE, 0xCA};
  CHECK(__crc32cw(acc, w) == crc32c_ref(acc, wb, 4), "__crc32cw");
  uint64_t d = 0x0123456789ABCDEFull;
  uint8_t db[8] = {0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01};
  CHECK(__crc32cd(acc, d) == crc32c_ref(acc, db, 8), "__crc32cd");
  return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloneon_MainActivity_probeNeon(JNIEnv* env,
                                                  jobject /*this*/) {
  std::string report = "ARM NEON intrinsic survey:\n";

  struct Probe {
    const char* label;
    bool (*fn)();
  };
  Probe probes[] = {
      {"Loads/Stores", probe_loads_stores},
      {"Integer arith", probe_int_arith},
      {"Saturating arith", probe_saturating},
      {"Compare", probe_compare},
      {"Min/Max/Abs/Neg", probe_minmax_absneg},
      {"Shifts", probe_shifts},
      {"Logical", probe_logical},
      {"Bit manipulation", probe_bitmanip},
      {"Reduce", probe_reduce},
      {"Pairwise", probe_pairwise},
      {"Permute", probe_permute},
      {"Convert", probe_convert},
      {"Lane access", probe_lanes},
      {"FP arith", probe_fp_arith},
      {"Polynomial mul", probe_polymul},
      {"Recip estimate", probe_recip_estimate},
      {"CRC32C", probe_crc32c},
  };

  bool all_ok = true;
  for (const auto& p : probes) {
    bool ok = p.fn();
    emit(&report, p.label, ok);
    if (!ok) all_ok = false;
  }
  report += all_ok ? "All NEON ops OK.\n"
                   : "One or more NEON ops FAILED.\n";

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
