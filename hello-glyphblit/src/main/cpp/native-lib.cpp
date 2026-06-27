// Integration-level probe for Skia's hand-written NEON A8 glyph blit
// (blit_mask_d32_a8_neon), the exact code path Chromium/Skia uses to composite
// an 8-bit coverage mask (a rasterized glyph) onto 32-bit destination pixels.
//
// Why this sample exists: under translation, Chromium renders web pages with
// images and layout correct but TEXT glyphs garbled, while Qt/FreeType text (a
// different blitter) renders cleanly. The difference is Skia's mask blit. This
// sample reproduces that exact NEON blit in isolation and self-checks it against
// a scalar reference computing the identical formula. On correct hardware (and a
// correct translator) the NEON and scalar results are bit-identical; if the
// translator mishandles one of the NEON ops (vld1/vmovl/vshrn/vsubw/vaddw/vld4/
// vst4/vmul), the two diverge and the probe reports FAIL with the first mismatch.
//
// The blit math (opaque color, from SkBlitMask_opts.h):
//   mask256 = mask + 1                       (SkAlpha255To256)
//   scale   = 256 - mask                     (opaque source)
//   out_ch  = (color_ch * mask256) >> 8 + (dev_ch * scale) >> 8   (per channel)
// applied to all four bytes of each pixel.

#include <android/log.h>
#include <arm_neon.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "helloglyphblit"

namespace {

// --- Skia's NEON helpers (verbatim from external/skia SkBlitMask_opts.h) ---

inline uint16x8_t SkAlpha255To256_neon8(uint8x8_t alpha) {
  return vaddw_u8(vdupq_n_u16(1), alpha);  // UADDW: 1 + alpha
}

inline uint8x8_t SkAlphaMul_neon8(uint8x8_t color, uint16x8_t scale) {
  // (color * scale) >> 8 : UXTL widen, MUL, SHRN narrow.
  return vshrn_n_u16(vmovl_u8(color) * scale, 8);
}

// Opaque variant of Skia's blit_mask_d32_a8_neon (isTranslucent == false).
// dst: width 32-bit pixels; mask: width coverage bytes. width must be >= 8.
void blit_mask_d32_a8_neon_opaque(uint8_t* dst,
                                  const uint8_t* mask,
                                  const uint8_t color[4],
                                  int width) {
  uint8x8x4_t vpmc;
  vpmc.val[0] = vdup_n_u8(color[0]);
  vpmc.val[1] = vdup_n_u8(color[1]);
  vpmc.val[2] = vdup_n_u8(color[2]);
  vpmc.val[3] = vdup_n_u8(color[3]);

  uint32_t* device = reinterpret_cast<uint32_t*>(dst);
  int w = width;
  while (w >= 8) {
    uint8x8_t vmask = vld1_u8(mask);                      // LD1
    uint16x8_t vmask256 = SkAlpha255To256_neon8(vmask);
    uint16x8_t vscale = vsubw_u8(vdupq_n_u16(256), vmask);  // USUBW: 256 - mask
    uint8x8x4_t vdev = vld4_u8(reinterpret_cast<uint8_t*>(device));  // LD4

    vdev.val[0] = vadd_u8(SkAlphaMul_neon8(vpmc.val[0], vmask256),
                          SkAlphaMul_neon8(vdev.val[0], vscale));
    vdev.val[1] = vadd_u8(SkAlphaMul_neon8(vpmc.val[1], vmask256),
                          SkAlphaMul_neon8(vdev.val[1], vscale));
    vdev.val[2] = vadd_u8(SkAlphaMul_neon8(vpmc.val[2], vmask256),
                          SkAlphaMul_neon8(vdev.val[2], vscale));
    vdev.val[3] = vadd_u8(SkAlphaMul_neon8(vpmc.val[3], vmask256),
                          SkAlphaMul_neon8(vdev.val[3], vscale));

    vst4_u8(reinterpret_cast<uint8_t*>(device), vdev);   // ST4
    mask += 8;
    device += 8;
    w -= 8;
  }
}

// Scalar reference computing the identical formula, per channel, with no SIMD.
// This is the oracle: on a correct translator it equals the NEON result exactly.
void blit_mask_d32_a8_scalar_opaque(uint8_t* dst,
                                    const uint8_t* mask,
                                    const uint8_t color[4],
                                    int width) {
  for (int p = 0; p < width; ++p) {
    unsigned m = mask[p];
    unsigned mask256 = m + 1u;
    unsigned scale = 256u - m;
    for (int b = 0; b < 4; ++b) {
      unsigned cov = (color[b] * mask256) >> 8;
      unsigned dev = (dst[p * 4 + b] * scale) >> 8;
      dst[p * 4 + b] = static_cast<uint8_t>(cov + dev);
    }
  }
}

// Skia's lowp-raster-pipeline div255 (verbatim from SkRasterPipeline_opts.h).
// This runs after EVERY coverage/alpha multiply in the lowp pipeline -- the path
// used to composite anti-aliased text onto an N32 (8-bit) surface. It uses the
// rounding-shift instructions URSRA (vrsraq_n_u16) and URSHR (vrshrq_n_u16),
// which the legacy blit above does NOT use (it uses plain SHRN). Opaque image
// blits skip div255 entirely (straight copy), so a div255 miscompile corrupts
// anti-aliased text while leaving images intact.
inline uint16x8_t div255_neon(uint16x8_t v) {
  return vrshrq_n_u16(vrsraq_n_u16(v, v, 8), 8);
}

// Scalar reference computing the identical formula with plain integer ops,
// faithfully WRAPPING at 16 bits exactly as the U16 NEON instructions do
// (URSRA/URSHR are modular, not saturating):
//   rsh   = (v + 128) >> 8           (URSHR of v)
//   step1 = (uint16_t)(v + rsh)      (URSRA accumulate, wraps at 16 bits)
//   out   = (step1 + 128) >> 8       (URSHR)
inline uint16_t div255_scalar(uint16_t v) {
  uint16_t rsh = static_cast<uint16_t>((v + 128u) >> 8);
  uint16_t step1 = static_cast<uint16_t>(v + rsh);  // intentional 16-bit wrap
  return static_cast<uint16_t>((step1 + 128u) >> 8);
}

// Check div255 over the range of values that actually occur in a lowp blend:
// each term is color[0..255] * scale[0..256], so products reach 255*256 = 65280.
// (We stop at 65280 because larger U16 values never arise and only differ by the
// modular wrap, which both NEON and the scalar reference above handle alike.)
std::string RunDiv255Check() {
  for (int base = 0; base <= 65280 - 8; base += 8) {
    uint16_t in[8];
    for (int i = 0; i < 8; ++i) in[i] = static_cast<uint16_t>(base + i);
    uint16x8_t vin = vld1q_u16(in);
    uint16_t out_neon[8];
    vst1q_u16(out_neon, div255_neon(vin));
    for (int i = 0; i < 8; ++i) {
      uint16_t want = div255_scalar(in[i]);
      if (out_neon[i] != want) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "FAIL div255(%u): neon=%u scalar=%u (round(v/255)=%u)",
                 in[i], out_neon[i], want, (in[i] + 127u) / 255u);
        return std::string(buf);
      }
    }
  }
  return std::string();
}

// Faithful replica of Skia's LOWP raster-pipeline coverage blend, using the
// SAME clang ext_vector_type (V<U16>) that Skia's SkRasterPipeline_opts.h uses,
// so clang generates the SAME NEON instruction schedule/register allocation as
// the real Chromium text path -- exercising codegen patterns that hand-written
// intrinsics above might not. This is the lowp lerp_u8 coverage stage:
//   lerp(from, to, t) = div255(from*inv(t) + to*t)   per channel,
// with from = dst pixel channel, to = src color channel, t = glyph coverage.
namespace lowp {
using U16 = uint16_t __attribute__((ext_vector_type(8)));
using U8 = uint8_t __attribute__((ext_vector_type(8)));

inline U16 div255(U16 v) {
  return vrshrq_n_u16(vrsraq_n_u16(v, v, 8), 8);
}
inline U16 inv(U16 v) { return static_cast<U16>(255) - v; }
inline U16 lerp(U16 from, U16 to, U16 t) { return div255(from * inv(t) + to * t); }

template <typename D, typename S>
inline D cast(S src) {
  return __builtin_convertvector(src, D);
}
}  // namespace lowp

// Run the lowp coverage blend over a row (ext-vector codegen) and compare to a
// scalar reference computing the identical formula with 16-bit wrap.
std::string RunLowpLerpCheck() {
  using lowp::U16;
  using lowp::U8;
  const int kN = 8 * 200;  // many 8-wide groups, varied inputs
  for (int base = 0; base + 8 <= kN; base += 8) {
    U8 from8, to8, t8;
    for (int i = 0; i < 8; ++i) {
      from8[i] = static_cast<uint8_t>((base + i) * 7 + 3);    // dst channel
      to8[i] = static_cast<uint8_t>((base + i) * 13 + 41);    // src channel
      t8[i] = static_cast<uint8_t>((base + i) * 29 + 17);     // coverage
    }
    U16 from = lowp::cast<U16>(from8);
    U16 to = lowp::cast<U16>(to8);
    U16 t = lowp::cast<U16>(t8);
    U16 out = lowp::lerp(from, to, t);
    for (int i = 0; i < 8; ++i) {
      // Scalar reference with the same 16-bit-wrapping div255.
      uint16_t f = from8[i], s = to8[i], cov = t8[i];
      uint16_t prod = static_cast<uint16_t>(f * static_cast<uint16_t>(255 - cov) +
                                            s * cov);
      uint16_t rsh = static_cast<uint16_t>((prod + 128u) >> 8);
      uint16_t step1 = static_cast<uint16_t>(prod + rsh);
      uint16_t want = static_cast<uint16_t>((step1 + 128u) >> 8);
      if (out[i] != want) {
        char buf[180];
        snprintf(buf, sizeof(buf),
                 "FAIL lerp(from=%u,to=%u,t=%u): neon=%u scalar=%u",
                 f, s, cov, out[i], want);
        return std::string(buf);
      }
    }
  }
  return std::string();
}

// High-register-pressure variant. The real Skia text blit is heavily inlined
// with many simultaneously-live SIMD vectors, which forces the lite translator
// to SPILL XMM registers to ThreadState memory (its pool is ~16). A spill/reload
// bug would miscompile such code while the low-pressure functions above stay
// correct. This keeps 24 independent div255 accumulator chains live across a
// barrier (the final reduction reads them all), forcing spills, then checks
// every lane against a scalar reference.
__attribute__((noinline)) std::string RunHighPressureCheck() {
  uint16x8_t acc[24];
  uint16_t seed[24][8];
  for (int k = 0; k < 24; ++k) {
    for (int i = 0; i < 8; ++i) {
      seed[k][i] = static_cast<uint16_t>((k * 31 + i * 251 + 7) & 0x3FFF);
    }
    acc[k] = vld1q_u16(seed[k]);
  }
  // Several rounds of div255-style rounding-shift math on all 24 chains. Because
  // every chain stays live into the final loop, the allocator cannot free them,
  // forcing register spills to memory.
  for (int round = 0; round < 4; ++round) {
    for (int k = 0; k < 24; ++k) {
      // round of: v = div255(v * (k+1)) + (round+1)   (keeps values bounded)
      uint16x8_t scaled = vmulq_u16(acc[k], vdupq_n_u16(static_cast<uint16_t>(k + 1)));
      acc[k] = vaddq_u16(div255_neon(scaled), vdupq_n_u16(static_cast<uint16_t>(round + 1)));
    }
  }
  // Scalar reference computing the identical sequence with 16-bit wrap.
  for (int k = 0; k < 24; ++k) {
    uint16_t out[8];
    vst1q_u16(out, acc[k]);
    for (int i = 0; i < 8; ++i) {
      uint16_t v = seed[k][i];
      for (int round = 0; round < 4; ++round) {
        uint16_t scaled = static_cast<uint16_t>(v * static_cast<uint16_t>(k + 1));
        uint16_t rsh = static_cast<uint16_t>((scaled + 128u) >> 8);
        uint16_t step1 = static_cast<uint16_t>(scaled + rsh);
        uint16_t d = static_cast<uint16_t>((step1 + 128u) >> 8);
        v = static_cast<uint16_t>(d + (round + 1));
      }
      if (out[i] != v) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "FAIL high-pressure chain %d lane %d: neon=%u scalar=%u",
                 k, i, out[i], v);
        return std::string(buf);
      }
    }
  }
  return std::string();
}

constexpr int kWidth = 64;  // multiple of 8 so the NEON loop runs fully

// Run the blit once with a known mask + color + destination and compare the
// NEON output to the scalar oracle byte-for-byte. Returns "" on success, or a
// human-readable mismatch description on failure.
std::string RunBlitCheck() {
  const uint8_t color[4] = {50, 100, 200, 255};  // arbitrary non-trivial color

  uint8_t mask[kWidth];
  for (int i = 0; i < kWidth; ++i) {
    // Varying coverage with both edges and partials, which is what makes the
    // anti-aliased glyph blit interesting (full + partial + zero coverage).
    mask[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
  }

  uint8_t dst_neon[kWidth * 4];
  uint8_t dst_scalar[kWidth * 4];
  for (int i = 0; i < kWidth * 4; ++i) {
    dst_neon[i] = static_cast<uint8_t>((i * 53 + 7) & 0xFF);  // known dst pattern
  }
  std::memcpy(dst_scalar, dst_neon, sizeof(dst_neon));

  blit_mask_d32_a8_neon_opaque(dst_neon, mask, color, kWidth);
  blit_mask_d32_a8_scalar_opaque(dst_scalar, mask, color, kWidth);

  for (int i = 0; i < kWidth * 4; ++i) {
    if (dst_neon[i] != dst_scalar[i]) {
      char buf[160];
      snprintf(buf, sizeof(buf),
               "FAIL at byte %d (pixel %d ch %d): neon=%u scalar=%u "
               "[mask=%u color=%u]",
               i, i / 4, i % 4, dst_neon[i], dst_scalar[i],
               mask[i / 4], color[i % 4]);
      return std::string(buf);
    }
  }
  return std::string();
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloglyphblit_MainActivity_probeGlyphBlit(JNIEnv* env,
                                                            jobject /*this*/) {
  std::string report = "Skia NEON A8 glyph-blit probe:\n";

  std::string blit_mismatch = RunBlitCheck();
  if (blit_mismatch.empty()) {
    report += "  legacy blit_mask_d32_a8_neon == scalar -> PASS\n";
  } else {
    report += "  legacy blit: " + blit_mismatch + " -> FAIL\n";
  }

  std::string div_mismatch = RunDiv255Check();
  if (div_mismatch.empty()) {
    report += "  lowp div255 (URSRA+URSHR) == scalar -> PASS\n";
  } else {
    report += "  lowp div255: " + div_mismatch + " -> FAIL\n";
    report += "  Skia lowp-pipeline div255 MISCOMPILED -> FAIL\n";
  }

  std::string lerp_mismatch = RunLowpLerpCheck();
  if (lerp_mismatch.empty()) {
    report += "  lowp lerp_u8 coverage blend (ext-vec) == scalar -> PASS\n";
  } else {
    report += "  lowp lerp_u8: " + lerp_mismatch + " -> FAIL\n";
    report += "  Skia lowp coverage blend MISCOMPILED -> FAIL\n";
  }

  std::string hp_mismatch = RunHighPressureCheck();
  if (hp_mismatch.empty()) {
    report += "  high-pressure SIMD (spill/reload) == scalar -> PASS\n";
  } else {
    report += "  high-pressure: " + hp_mismatch + " -> FAIL\n";
    report += "  SIMD register spill/reload MISCOMPILED -> FAIL\n";
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
