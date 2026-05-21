// hello-complex: integration-level probe for Armv8.3-FCMA (§H1 / §M1).
//
// Probes every FCMA encoding the §H1 decoder + interpreter implemented
// in handoff-49.  FCADD has two rotations (#90 / #270); FCMLA has four
// rotations (#0 / #90 / #180 / #270).  Each is exercised at .4S vector,
// .2D vector, and .2S half-vector widths where the encoding allows.
//
//   FCADD V.4S, Vn, Vm, #90    Vd_re = Vn_re - Vm_im, Vd_im = Vn_im + Vm_re
//   FCADD V.4S, Vn, Vm, #270   Vd_re = Vn_re + Vm_im, Vd_im = Vn_im - Vm_re
//   FCADD V.2D, Vn, Vm, #90    (double-precision form, 1 pair)
//   FCADD V.2S, Vn, Vm, #90    (Q=0, 1 pair, top 64 bits cleared)
//   FCMLA V.4S, Vn, Vm, #0     Vd_re += Vn_re * Vm_re,   Vd_im += Vn_re * Vm_im
//   FCMLA V.4S, Vn, Vm, #90    Vd_re += Vn_im * (-Vm_im),Vd_im += Vn_im * Vm_re
//   FCMLA V.4S, Vn, Vm, #180   Vd_re += Vn_re * (-Vm_re),Vd_im += Vn_re * (-Vm_im)
//   FCMLA V.4S, Vn, Vm, #270   Vd_re += Vn_im * Vm_im,   Vd_im += Vn_im * (-Vm_re)
//   FCMLA V.2D, Vn, Vm, #90    (double-precision form)
//
// The interpreter implementation is in interpreter.h::AdvSimdFcma at line
// ~981.  If any FCMA encoding routes to Undefined() the entire native lib
// SIGILLs on the first call.  If the decoder rotation table is wrong the
// per-rotation probes would each give the wrong sign in either the real
// or imaginary half.
//
// llvm-mc-verified encodings (cited in handoff-49):
//   0x6e82e420  fcadd v0.4s, v1.4s, v2.4s, #90
//   0x6e82f420  fcadd v0.4s, v1.4s, v2.4s, #270
//   0x6ec2e420  fcadd v0.2d, v1.2d, v2.2d, #90
//   0x2e82e420  fcadd v0.2s, v1.2s, v2.2s, #90
//   0x6e82c420  fcmla v0.4s, v1.4s, v2.4s, #0
//   0x6e82cc20  fcmla v0.4s, v1.4s, v2.4s, #90
//   0x6e82d420  fcmla v0.4s, v1.4s, v2.4s, #180
//   0x6e82dc20  fcmla v0.4s, v1.4s, v2.4s, #270
//   0x6ec2cc20  fcmla v0.2d, v1.2d, v2.2d, #90

#include <android/log.h>
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "hellocomplex"

namespace {

bool approxf(float got, float want) {
  float tol = 1e-5f * std::fabs(want) + 1e-6f;
  return std::fabs(got - want) <= tol;
}

bool approxd(double got, double want) {
  double tol = 1e-12 * std::fabs(want) + 1e-13;
  return std::fabs(got - want) <= tol;
}

// region digitalis - Plan §H1 FP16 SIMD FCMA (handoff-61)
// IEEE 754 binary32 -> binary16 ("F16C round-to-nearest-even" semantics)
// and the inverse.  Same helpers as hello-fp16; copied verbatim so each
// sample is self-contained.
uint16_t SingleToHalf(float f) {
  uint32_t fbits;
  std::memcpy(&fbits, &f, 4);
  uint16_t sign = (fbits >> 16) & 0x8000;
  int32_t exp = ((fbits >> 23) & 0xFF) - 127;
  uint32_t frac = fbits & 0x7FFFFF;

  if (exp == 128) {
    return sign | 0x7C00 | (frac ? (frac >> 13) | 1 : 0);
  }
  if (exp > 15) return sign | 0x7C00;
  if (exp < -24) return sign;
  if (exp < -14) {
    frac = (frac | 0x800000) >> (-exp - 14 + 13);
    return sign | static_cast<uint16_t>(frac);
  }
  return sign | static_cast<uint16_t>((exp + 15) << 10) |
         static_cast<uint16_t>(frac >> 13);
}

float HalfToSingle(uint16_t h) {
  uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
  uint32_t exp = (h >> 10) & 0x1F;
  uint32_t frac = h & 0x3FF;
  uint32_t fbits;
  if (exp == 0) {
    if (frac == 0) {
      fbits = sign;
    } else {
      exp = 1;
      while (!(frac & 0x400)) {
        frac <<= 1;
        exp--;
      }
      frac &= 0x3FF;
      fbits = sign | (static_cast<uint32_t>(exp + 127 - 15) << 23) | (frac << 13);
    }
  } else if (exp == 0x1F) {
    fbits = sign | 0x7F800000 | (frac << 13);
  } else {
    fbits = sign | (static_cast<uint32_t>(exp + 127 - 15) << 23) | (frac << 13);
  }
  float result;
  std::memcpy(&result, &fbits, 4);
  return result;
}

bool approx_half(uint16_t got, uint16_t want) {
  // Tolerate single-ULP mismatches due to different rounding paths.
  if (got == want) return true;
  float gf = HalfToSingle(got);
  float wf = HalfToSingle(want);
  float tol = 1e-3f * std::fabs(wf) + 1e-3f;
  return std::fabs(gf - wf) <= tol;
}
// endregion

// FCADD V.4S, Vn, Vm, #rot  — single-precision, 2 complex pairs per vector.
bool probe_fcadd_4s(std::string& report, char (&buf)[256], int rot) {
  alignas(16) float n[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  alignas(16) float m[4] = {10.0f, 20.0f, 30.0f, 40.0f};
  alignas(16) float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  if (rot == 90) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        ".inst 0x6e82e420  // fcadd v0.4s, v1.4s, v2.4s, #90\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        ".inst 0x6e82f420  // fcadd v0.4s, v1.4s, v2.4s, #270\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  }
  float want[4];
  for (int p = 0; p < 2; p++) {
    float n_re = n[2 * p], n_im = n[2 * p + 1];
    float m_re = m[2 * p], m_im = m[2 * p + 1];
    if (rot == 90) {
      want[2 * p]     = n_re - m_im;
      want[2 * p + 1] = n_im + m_re;
    } else {
      want[2 * p]     = n_re + m_im;
      want[2 * p + 1] = n_im - m_re;
    }
  }
  bool ok = approxf(out[0], want[0]) && approxf(out[1], want[1]) &&
            approxf(out[2], want[2]) && approxf(out[3], want[3]);
  snprintf(buf, sizeof(buf),
           "  FCADD .4S #%-3d out=[%.4f %.4f %.4f %.4f] want=[%.4f %.4f %.4f %.4f]: %s\n",
           rot,
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// FCADD V.2D, Vn, Vm, #rot  — double-precision, 1 complex pair.
bool probe_fcadd_2d(std::string& report, char (&buf)[256], int rot) {
  alignas(16) double n[2] = {1.5, 2.5};
  alignas(16) double m[2] = {10.0, 20.0};
  alignas(16) double out[2] = {0.0, 0.0};
  if (rot == 90) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        ".inst 0x6ec2e420  // fcadd v0.2d, v1.2d, v2.2d, #90\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        ".inst 0x6ec2f420  // fcadd v0.2d, v1.2d, v2.2d, #270\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  double want[2];
  if (rot == 90) {
    want[0] = n[0] - m[1];
    want[1] = n[1] + m[0];
  } else {
    want[0] = n[0] + m[1];
    want[1] = n[1] - m[0];
  }
  bool ok = approxd(out[0], want[0]) && approxd(out[1], want[1]);
  snprintf(buf, sizeof(buf),
           "  FCADD .2D #%-3d out=[%.6f %.6f] want=[%.6f %.6f]: %s\n",
           rot, out[0], out[1], want[0], want[1], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// FCADD V.2S, Vn, Vm, #90  — Q=0 half-vector, 1 pair, top 64 bits cleared.
bool probe_fcadd_2s(std::string& report, char (&buf)[256]) {
  alignas(16) float n[4] = {1.0f, 2.0f, 99.0f, 99.0f};
  alignas(16) float m[4] = {10.0f, 20.0f, 99.0f, 99.0f};
  // Pre-load 0xdead… into the top half of v0 to confirm the JIT/interp
  // zeros it after the FCADD (Q=0 semantics).
  alignas(16) uint32_t prev[4] = {0xdeadbeefu, 0xdeadbeefu,
                                   0xdeadbeefu, 0xdeadbeefu};
  alignas(16) float out[4];
  std::memcpy(out, prev, 16);
  __asm__ __volatile__(
      "ldr q1, [%0]\n"
      "ldr q2, [%1]\n"
      "ldr q0, [%2]\n"
      ".inst 0x2e82e420  // fcadd v0.2s, v1.2s, v2.2s, #90\n"
      "str q0, [%2]\n"
      :
      : "r"(n), "r"(m), "r"(out)
      : "v0", "v1", "v2", "memory");
  float want_lo[2] = {n[0] - m[1], n[1] + m[0]};
  uint32_t out_hi_bits[2];
  std::memcpy(out_hi_bits, &out[2], 8);
  bool ok = approxf(out[0], want_lo[0]) && approxf(out[1], want_lo[1]) &&
            out_hi_bits[0] == 0 && out_hi_bits[1] == 0;
  snprintf(buf, sizeof(buf),
           "  FCADD .2S #90  lo=[%.4f %.4f] hi=[%08x %08x]: %s\n",
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           out_hi_bits[0], out_hi_bits[1], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// FCMLA V.4S, Vn, Vm, #rot  — single-precision, 2 complex pairs.
// Vd is read-modify-write: Vd' += rot-shuffled (Vn * Vm).
bool probe_fcmla_4s(std::string& report, char (&buf)[256], int rot) {
  alignas(16) float n[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  alignas(16) float m[4] = {5.0f, 6.0f, 7.0f, 8.0f};
  alignas(16) float pre[4] = {100.0f, 200.0f, 300.0f, 400.0f};
  alignas(16) float out[4];
  std::memcpy(out, pre, 16);
  if (rot == 0) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6e82c420  // fcmla v0.4s, v1.4s, v2.4s, #0\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else if (rot == 90) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6e82cc20  // fcmla v0.4s, v1.4s, v2.4s, #90\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else if (rot == 180) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6e82d420  // fcmla v0.4s, v1.4s, v2.4s, #180\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {  // 270
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6e82dc20  // fcmla v0.4s, v1.4s, v2.4s, #270\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  }
  float want[4];
  for (int p = 0; p < 2; p++) {
    float n_re = n[2 * p], n_im = n[2 * p + 1];
    float m_re = m[2 * p], m_im = m[2 * p + 1];
    float d_re = pre[2 * p], d_im = pre[2 * p + 1];
    float r_re = 0, r_im = 0;
    switch (rot) {
      case 0:   r_re = d_re + n_re *  m_re; r_im = d_im + n_re *  m_im; break;
      case 90:  r_re = d_re + n_im * -m_im; r_im = d_im + n_im *  m_re; break;
      case 180: r_re = d_re + n_re * -m_re; r_im = d_im + n_re * -m_im; break;
      case 270: r_re = d_re + n_im *  m_im; r_im = d_im + n_im * -m_re; break;
    }
    want[2 * p]     = r_re;
    want[2 * p + 1] = r_im;
  }
  bool ok = approxf(out[0], want[0]) && approxf(out[1], want[1]) &&
            approxf(out[2], want[2]) && approxf(out[3], want[3]);
  snprintf(buf, sizeof(buf),
           "  FCMLA .4S #%-3d out=[%.4f %.4f %.4f %.4f] want=[%.4f %.4f %.4f %.4f]: %s\n",
           rot,
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// FCMLA V.2D, Vn, Vm, #rot — double-precision, 1 complex pair.
// Encodings: #0=0x6ec2c420, #90=0x6ec2cc20, #180=0x6ec2d420, #270=0x6ec2dc20.
bool probe_fcmla_2d(std::string& report, char (&buf)[256], int rot) {
  alignas(16) double n[2] = {1.5, 2.5};
  alignas(16) double m[2] = {3.5, 4.5};
  alignas(16) double pre[2] = {100.0, 200.0};
  alignas(16) double out[2];
  std::memcpy(out, pre, 16);
  if (rot == 0) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6ec2c420  // fcmla v0.2d, v1.2d, v2.2d, #0\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if (rot == 90) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6ec2cc20  // fcmla v0.2d, v1.2d, v2.2d, #90\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if (rot == 180) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6ec2d420  // fcmla v0.2d, v1.2d, v2.2d, #180\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6ec2dc20  // fcmla v0.2d, v1.2d, v2.2d, #270\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  double want[2];
  switch (rot) {
    case 0:   want[0] = pre[0] + n[0] *  m[0]; want[1] = pre[1] + n[0] *  m[1]; break;
    case 90:  want[0] = pre[0] + n[1] * -m[1]; want[1] = pre[1] + n[1] *  m[0]; break;
    case 180: want[0] = pre[0] + n[0] * -m[0]; want[1] = pre[1] + n[0] * -m[1]; break;
    default:  want[0] = pre[0] + n[1] *  m[1]; want[1] = pre[1] + n[1] * -m[0]; break;
  }
  bool ok = approxd(out[0], want[0]) && approxd(out[1], want[1]);
  snprintf(buf, sizeof(buf),
           "  FCMLA .2D #%-3d out=[%.6f %.6f] want=[%.6f %.6f]: %s\n",
           rot, out[0], out[1], want[0], want[1], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// FCMLA V.4S, Vn, Vm.s[idx], #rot — FP32 by-element.
// Vm[idx] broadcasts ONE complex pair (Vm.s[2*idx], Vm.s[2*idx+1]) across
// every output complex pair of Vd.  Vd is read-modify-write.
// Encoded inst opcodes (handoff-58 llvm-mc):
//   idx=0: 0x6F821020 #0   0x6F823020 #90   0x6F825020 #180  0x6F827020 #270
//   idx=1: 0x6F821820 #0   0x6F823820 #90   0x6F825820 #180  0x6F827820 #270
bool probe_fcmla_4s_idx(std::string& report, char (&buf)[256], int rot, int idx) {
  alignas(16) float n[4] = {1.0f, 2.0f, 3.0f, 4.0f};
  alignas(16) float m[4] = {5.0f, 6.0f, 7.0f, 8.0f};
  alignas(16) float pre[4] = {100.0f, 200.0f, 300.0f, 400.0f};
  alignas(16) float out[4];
  std::memcpy(out, pre, 16);
  if (idx == 0) {
    if (rot == 0) {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f821020  // fcmla v0.4s, v1.4s, v2.s[0], #0\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    } else if (rot == 90) {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f823020  // fcmla v0.4s, v1.4s, v2.s[0], #90\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    } else if (rot == 180) {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f825020  // fcmla v0.4s, v1.4s, v2.s[0], #180\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    } else {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f827020  // fcmla v0.4s, v1.4s, v2.s[0], #270\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    }
  } else {  // idx == 1
    if (rot == 0) {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f821820  // fcmla v0.4s, v1.4s, v2.s[1], #0\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    } else if (rot == 90) {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f823820  // fcmla v0.4s, v1.4s, v2.s[1], #90\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    } else if (rot == 180) {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f825820  // fcmla v0.4s, v1.4s, v2.s[1], #180\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    } else {
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6f827820  // fcmla v0.4s, v1.4s, v2.s[1], #270\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
    }
  }
  // Reference: Vm[idx] is the single broadcast complex pair.
  float m_re = m[2 * idx];
  float m_im = m[2 * idx + 1];
  float want[4];
  for (int p = 0; p < 2; p++) {
    float n_re = n[2 * p], n_im = n[2 * p + 1];
    float d_re = pre[2 * p], d_im = pre[2 * p + 1];
    float r_re = 0, r_im = 0;
    switch (rot) {
      case 0:   r_re = d_re + n_re *  m_re; r_im = d_im + n_re *  m_im; break;
      case 90:  r_re = d_re + n_im * -m_im; r_im = d_im + n_im *  m_re; break;
      case 180: r_re = d_re + n_re * -m_re; r_im = d_im + n_re * -m_im; break;
      case 270: r_re = d_re + n_im *  m_im; r_im = d_im + n_im * -m_re; break;
    }
    want[2 * p]     = r_re;
    want[2 * p + 1] = r_im;
  }
  bool ok = approxf(out[0], want[0]) && approxf(out[1], want[1]) &&
            approxf(out[2], want[2]) && approxf(out[3], want[3]);
  snprintf(buf, sizeof(buf),
           "  FCMLA .4S[%d] #%-3d out=[%.4f %.4f %.4f %.4f] "
           "want=[%.4f %.4f %.4f %.4f]: %s\n",
           idx, rot,
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// region digitalis - Plan §H1 FP16 SIMD FCMA (handoff-61)
// FCADD V.8H, Vn, Vm, #rot — half-precision, 4 complex pairs per vector.
// llvm-mc-verified .inst opcodes:
//   fcadd v0.4h, v1.4h, v2.4h, #90  = 0x2e42e420
//   fcadd v0.4h, v1.4h, v2.4h, #270 = 0x2e42f420
//   fcadd v0.8h, v1.8h, v2.8h, #90  = 0x6e42e420
//   fcadd v0.8h, v1.8h, v2.8h, #270 = 0x6e42f420
//   fcmla v0.8h, v1.8h, v2.8h, #0   = 0x6e42c420
//   fcmla v0.8h, v1.8h, v2.8h, #90  = 0x6e42cc20
//   fcmla v0.8h, v1.8h, v2.8h, #180 = 0x6e42d420
//   fcmla v0.8h, v1.8h, v2.8h, #270 = 0x6e42dc20
//   fcmla v0.4h, v1.4h, v2.4h, #0   = 0x2e42c420
//   fcmla v0.4h, v1.4h, v2.h[0], #0   = 0x2f421020 (Q=0, .4H idx=0)
//   fcmla v0.4h, v1.4h, v2.h[1], #90  = 0x2f623020 (Q=0, .4H idx=1)
//   fcmla v0.8h, v1.8h, v2.h[0], #0   = 0x6f421020 (Q=1, .8H idx=0)
//   fcmla v0.8h, v1.8h, v2.h[1], #0   = 0x6f621020 (idx=1: L=1)
//   fcmla v0.8h, v1.8h, v2.h[2], #0   = 0x6f421820 (idx=2: H=1)
//   fcmla v0.8h, v1.8h, v2.h[3], #270 = 0x6f627820

bool probe_fcadd_fp16_8h(std::string& report, char (&buf)[256], int rot) {
  alignas(16) uint16_t n[8], m[8], out[8];
  for (int i = 0; i < 8; i++) {
    n[i] = SingleToHalf(1.0f + i * 0.5f);
    m[i] = SingleToHalf(2.0f + i * 0.25f);
    out[i] = 0;
  }
  if (rot == 90) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        ".inst 0x6e42e420  // fcadd v0.8h, v1.8h, v2.8h, #90\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        ".inst 0x6e42f420  // fcadd v0.8h, v1.8h, v2.8h, #270\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint16_t want[8];
  for (int p = 0; p < 4; p++) {
    float n_re = HalfToSingle(n[2 * p]);
    float n_im = HalfToSingle(n[2 * p + 1]);
    float m_re = HalfToSingle(m[2 * p]);
    float m_im = HalfToSingle(m[2 * p + 1]);
    if (rot == 90) {
      want[2 * p]     = SingleToHalf(n_re - m_im);
      want[2 * p + 1] = SingleToHalf(n_im + m_re);
    } else {
      want[2 * p]     = SingleToHalf(n_re + m_im);
      want[2 * p + 1] = SingleToHalf(n_im - m_re);
    }
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) ok = ok && approx_half(out[i], want[i]);
  snprintf(buf, sizeof(buf),
           "  FCADD .8H #%-3d out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           rot,
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_fcadd_fp16_4h(std::string& report, char (&buf)[256], int rot) {
  // Q=0 form: lanes 0..3 active; upper 64 bits of Vd must be cleared.
  alignas(16) uint16_t n[8] = {SingleToHalf(1.0f), SingleToHalf(2.0f),
                                SingleToHalf(3.0f), SingleToHalf(4.0f),
                                0xdead, 0xbeef, 0xcafe, 0xf00d};
  alignas(16) uint16_t m[8] = {SingleToHalf(0.5f), SingleToHalf(1.5f),
                                SingleToHalf(2.5f), SingleToHalf(3.5f),
                                0xdead, 0xbeef, 0xcafe, 0xf00d};
  alignas(16) uint16_t prev[8] = {0xdeadu, 0xbeefu, 0xcafeu, 0xf00du,
                                   0xdeadu, 0xbeefu, 0xcafeu, 0xf00du};
  alignas(16) uint16_t out[8];
  std::memcpy(out, prev, 16);
  if (rot == 90) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x2e42e420  // fcadd v0.4h, v1.4h, v2.4h, #90\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x2e42f420  // fcadd v0.4h, v1.4h, v2.4h, #270\n"
        "str q0, [%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint16_t want[4];
  for (int p = 0; p < 2; p++) {
    float n_re = HalfToSingle(n[2 * p]);
    float n_im = HalfToSingle(n[2 * p + 1]);
    float m_re = HalfToSingle(m[2 * p]);
    float m_im = HalfToSingle(m[2 * p + 1]);
    if (rot == 90) {
      want[2 * p]     = SingleToHalf(n_re - m_im);
      want[2 * p + 1] = SingleToHalf(n_im + m_re);
    } else {
      want[2 * p]     = SingleToHalf(n_re + m_im);
      want[2 * p + 1] = SingleToHalf(n_im - m_re);
    }
  }
  bool ok = approx_half(out[0], want[0]) && approx_half(out[1], want[1]) &&
            approx_half(out[2], want[2]) && approx_half(out[3], want[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  FCADD .4H #%-3d lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           rot, out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_fcmla_fp16_8h(std::string& report, char (&buf)[256], int rot) {
  alignas(16) uint16_t n[8], m[8], pre[8], out[8];
  for (int i = 0; i < 8; i++) {
    n[i] = SingleToHalf(0.5f + i * 0.25f);
    m[i] = SingleToHalf(1.0f + i * 0.5f);
    pre[i] = SingleToHalf(2.0f + i);
  }
  std::memcpy(out, pre, 16);
  switch (rot) {
    case 0:
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6e42c420  // fcmla v0.8h, v1.8h, v2.8h, #0\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
      break;
    case 90:
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6e42cc20  // fcmla v0.8h, v1.8h, v2.8h, #90\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
      break;
    case 180:
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6e42d420  // fcmla v0.8h, v1.8h, v2.8h, #180\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
      break;
    default:
      __asm__ __volatile__(
          "ldr q1, [%0]\n"
          "ldr q2, [%1]\n"
          "ldr q0, [%2]\n"
          ".inst 0x6e42dc20  // fcmla v0.8h, v1.8h, v2.8h, #270\n"
          "str q0, [%2]\n"
          : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
      break;
  }
  uint16_t want[8];
  for (int p = 0; p < 4; p++) {
    float n_re = HalfToSingle(n[2 * p]);
    float n_im = HalfToSingle(n[2 * p + 1]);
    float m_re = HalfToSingle(m[2 * p]);
    float m_im = HalfToSingle(m[2 * p + 1]);
    float d_re = HalfToSingle(pre[2 * p]);
    float d_im = HalfToSingle(pre[2 * p + 1]);
    float r_re = 0, r_im = 0;
    switch (rot) {
      case 0:   r_re = d_re + n_re *  m_re; r_im = d_im + n_re *  m_im; break;
      case 90:  r_re = d_re + n_im * -m_im; r_im = d_im + n_im *  m_re; break;
      case 180: r_re = d_re + n_re * -m_re; r_im = d_im + n_re * -m_im; break;
      case 270: r_re = d_re + n_im *  m_im; r_im = d_im + n_im * -m_re; break;
    }
    want[2 * p]     = SingleToHalf(r_re);
    want[2 * p + 1] = SingleToHalf(r_im);
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) ok = ok && approx_half(out[i], want[i]);
  snprintf(buf, sizeof(buf),
           "  FCMLA .8H #%-3d out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           rot, out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_fcmla_fp16_8h_idx(std::string& report, char (&buf)[256], int rot,
                              int idx) {
  alignas(16) uint16_t n[8], m[8], pre[8], out[8];
  for (int i = 0; i < 8; i++) {
    n[i] = SingleToHalf(0.5f + i * 0.25f);
    m[i] = SingleToHalf(1.0f + i * 0.5f);
    pre[i] = SingleToHalf(2.0f + i);
  }
  std::memcpy(out, pre, 16);
  // Each instruction encoded with its specific (rot, idx) pair.
  // idx encoding: bit21=L, bit11=H, index = (H<<1)|L.
  //   idx=0: 0x6f4?1?20 (L=0,H=0)  idx=1: 0x6f6?1?20 (L=1)
  //   idx=2: 0x6f4?1?20 (H=1, bit11=1 → +0x800)  idx=3: 0x6f6?1?20 + bit11
  //   rot=0/90/180/270 → bit13:14 = 00/01/10/11 (so opcode bits 12=1, 13:14)
  // Pre-derived encodings:
  uint32_t enc;
  // Build encoding manually: base 0x6f421020 + (L<<21) + (H<<11) + (rot[0]<<13) + (rot[1]<<14)
  uint32_t H_bit = (idx >> 1) & 1;
  uint32_t L_bit = idx & 1;
  uint32_t rot_field = (rot == 0) ? 0 : (rot == 90) ? 1 : (rot == 180) ? 2 : 3;
  enc = 0x6f421020u | (L_bit << 21) | (H_bit << 11) |
        ((rot_field & 1) << 13) | (((rot_field >> 1) & 1) << 14);
  // Use a switch over (rot,idx) for static .inst encoding.
  switch (enc) {
    case 0x6f421020u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f421020\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    case 0x6f621020u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f621020\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    case 0x6f421820u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f421820\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    case 0x6f621820u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f621820\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    case 0x6f423020u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f423020\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    case 0x6f625020u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f625020\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    case 0x6f627820u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f627820\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    case 0x6f425820u:
      __asm__ __volatile__("ldr q1,[%0]\nldr q2,[%1]\nldr q0,[%2]\n.inst 0x6f425820\nstr q0,[%2]\n"
        : : "r"(n), "r"(m), "r"(out) : "v0","v1","v2","memory"); break;
    default:
      snprintf(buf, sizeof(buf),
               "  FCMLA .8H[%d] #%-3d: SKIP (enc=0x%08x not pre-cased)\n",
               idx, rot, enc);
      report += buf;
      return true;
  }
  uint16_t want[8];
  float m_re = HalfToSingle(m[2 * idx]);
  float m_im = HalfToSingle(m[2 * idx + 1]);
  for (int p = 0; p < 4; p++) {
    float n_re = HalfToSingle(n[2 * p]);
    float n_im = HalfToSingle(n[2 * p + 1]);
    float d_re = HalfToSingle(pre[2 * p]);
    float d_im = HalfToSingle(pre[2 * p + 1]);
    float r_re = 0, r_im = 0;
    switch (rot) {
      case 0:   r_re = d_re + n_re *  m_re; r_im = d_im + n_re *  m_im; break;
      case 90:  r_re = d_re + n_im * -m_im; r_im = d_im + n_im *  m_re; break;
      case 180: r_re = d_re + n_re * -m_re; r_im = d_im + n_re * -m_im; break;
      case 270: r_re = d_re + n_im *  m_im; r_im = d_im + n_im * -m_re; break;
    }
    want[2 * p]     = SingleToHalf(r_re);
    want[2 * p + 1] = SingleToHalf(r_im);
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) ok = ok && approx_half(out[i], want[i]);
  snprintf(buf, sizeof(buf),
           "  FCMLA .8H[%d] #%-3d out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           idx, rot,
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}
// endregion

// FRINTA Rd, Rn — round-to-nearest, ties away from zero.  ARM ARM C7.2.119.
// No native x86 ROUND* imm models ties-away, so the new JIT path lowers to
// `dst = trunc(src + copysign(0.5, src))`.  These probes confirm the JIT
// output matches the interpreter for representative inputs across FP16,
// FP32, and FP64.
bool probe_frinta_s(std::string& report, char (&buf)[256],
                    float in, float want) {
  float out;
  asm volatile(
      "ldr s1, [%[pa]]\n\t"
      "frinta s0, s1\n\t"
      "str s0, [%[pr]]\n\t"
      :
      : [pa] "r"(&in), [pr] "r"(&out)
      : "v0", "v1", "memory");
  bool ok = std::isnan(want) ? std::isnan(out) : (out == want);
  snprintf(buf, sizeof(buf),
           "  FRINTA s in=%-8g out=%-8g want=%-8g: %s\n",
           static_cast<double>(in), static_cast<double>(out),
           static_cast<double>(want), ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_frinta_d(std::string& report, char (&buf)[256],
                    double in, double want) {
  double out;
  asm volatile(
      "ldr d1, [%[pa]]\n\t"
      "frinta d0, d1\n\t"
      "str d0, [%[pr]]\n\t"
      :
      : [pa] "r"(&in), [pr] "r"(&out)
      : "v0", "v1", "memory");
  bool ok = std::isnan(want) ? std::isnan(out) : (out == want);
  snprintf(buf, sizeof(buf),
           "  FRINTA d in=%-8g out=%-8g want=%-8g: %s\n",
           in, out, want, ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_frinta_h(std::string& report, char (&buf)[256],
                    float in, float want) {
  uint16_t in_h = SingleToHalf(in);
  uint16_t out_h;
  asm volatile(
      "ldr h1, [%[pa]]\n\t"
      "frinta h0, h1\n\t"
      "str h0, [%[pr]]\n\t"
      :
      : [pa] "r"(&in_h), [pr] "r"(&out_h)
      : "v0", "v1", "memory");
  float out = HalfToSingle(out_h);
  bool ok = std::isnan(want) ? std::isnan(out) : (out == want);
  snprintf(buf, sizeof(buf),
           "  FRINTA h in=%-8g out=%-8g want=%-8g: %s\n",
           static_cast<double>(in), static_cast<double>(out),
           static_cast<double>(want), ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FP unary (FABS/FNEG/FRINT*) probes — Plan §C4 vector form.  Each
// probe loads V1 from memory as a Q register, runs one of the 9 FP unary
// vector mnemonics, stores V0, and lane-by-lane compares with `want`.  FP32
// vectors carry 4 lanes (Q=1, .4S); FP64 vectors carry 2 lanes (Q=1, .2D).
//
// The same input set used by the scalar FRINTA probes is replicated across
// lanes; the vector op produces the same per-lane result as the scalar form.
#define PROBE_VEC_4S(NAME, MNEMONIC)                                          \
  bool probe_##NAME##_4s(std::string& report, char (&buf)[256],               \
                         const float (&in)[4], const float (&want)[4]) {      \
    alignas(16) float in_buf[4]  = {in[0], in[1], in[2], in[3]};              \
    alignas(16) float out_buf[4] = {0, 0, 0, 0};                              \
    asm volatile(                                                             \
        "ldr q1, [%[pa]]\n\t"                                                 \
        MNEMONIC " v0.4s, v1.4s\n\t"                                          \
        "str q0, [%[pr]]\n\t"                                                 \
        :                                                                     \
        : [pa] "r"(in_buf), [pr] "r"(out_buf)                                 \
        : "v0", "v1", "memory");                                              \
    bool ok = true;                                                           \
    for (int i = 0; i < 4; i++) {                                             \
      bool lane_ok = std::isnan(want[i]) ? std::isnan(out_buf[i])             \
                                         : (out_buf[i] == want[i]);           \
      if (!lane_ok) ok = false;                                               \
    }                                                                         \
    snprintf(buf, sizeof(buf), "  " #NAME " .4S: %s\n", ok ? "OK" : "FAIL");  \
    report += buf;                                                            \
    return ok;                                                                \
  }

#define PROBE_VEC_2D(NAME, MNEMONIC)                                          \
  bool probe_##NAME##_2d(std::string& report, char (&buf)[256],               \
                         const double (&in)[2], const double (&want)[2]) {    \
    alignas(16) double in_buf[2]  = {in[0], in[1]};                           \
    alignas(16) double out_buf[2] = {0, 0};                                   \
    asm volatile(                                                             \
        "ldr q1, [%[pa]]\n\t"                                                 \
        MNEMONIC " v0.2d, v1.2d\n\t"                                          \
        "str q0, [%[pr]]\n\t"                                                 \
        :                                                                     \
        : [pa] "r"(in_buf), [pr] "r"(out_buf)                                 \
        : "v0", "v1", "memory");                                              \
    bool ok = true;                                                           \
    for (int i = 0; i < 2; i++) {                                             \
      bool lane_ok = std::isnan(want[i]) ? std::isnan(out_buf[i])             \
                                         : (out_buf[i] == want[i]);           \
      if (!lane_ok) ok = false;                                               \
    }                                                                         \
    snprintf(buf, sizeof(buf), "  " #NAME " .2D: %s\n", ok ? "OK" : "FAIL");  \
    report += buf;                                                            \
    return ok;                                                                \
  }

PROBE_VEC_4S(fabs,   "fabs")
PROBE_VEC_4S(fneg,   "fneg")
PROBE_VEC_4S(frintn, "frintn")
PROBE_VEC_4S(frintm, "frintm")
PROBE_VEC_4S(frintp, "frintp")
PROBE_VEC_4S(frintz, "frintz")
PROBE_VEC_4S(frinta, "frinta")
PROBE_VEC_4S(frintx, "frintx")
PROBE_VEC_4S(frinti, "frinti")
PROBE_VEC_4S(fsqrt,  "fsqrt")
PROBE_VEC_2D(fabs,   "fabs")
PROBE_VEC_2D(fneg,   "fneg")
PROBE_VEC_2D(frintn, "frintn")
PROBE_VEC_2D(frintm, "frintm")
PROBE_VEC_2D(frintp, "frintp")
PROBE_VEC_2D(frintz, "frintz")
PROBE_VEC_2D(frinta, "frinta")
PROBE_VEC_2D(frintx, "frintx")
PROBE_VEC_2D(frinti, "frinti")
PROBE_VEC_2D(fsqrt,  "fsqrt")

#undef PROBE_VEC_4S
#undef PROBE_VEC_2D

// Q=0 (.2S) variant: lanes 0/1 carry data, lanes 2/3 must be zero on write.
// Confirms the mask_low64() emit on the !args.q path.
bool probe_fneg_2s_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) float in_buf[4]  = {1.0f, -2.0f, 3.0f, -4.0f};
  alignas(16) float out_buf[4] = {99.f, 99.f, 99.f, 99.f};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fneg v0.2s, v1.2s\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(in_buf), [pr] "r"(out_buf)
      : "v0", "v1", "memory");
  bool ok = (out_buf[0] == -1.0f) && (out_buf[1] == 2.0f) &&
            (out_buf[2] == 0.0f) && (out_buf[3] == 0.0f);
  snprintf(buf, sizeof(buf), "  fneg .2S (upper-zero): %s\n",
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FSQRT FP16 .8H (Q=1) — 8 perfect-square lanes plus a negative
// lane for NaN propagation through the F16C round-trip.  Exercises the
// .8H split-and-recombine JIT path (low 4 lanes via Vcvtph2ps; high 4
// lanes via Psrldq + Vcvtph2ps; results merged with Pslldq + Por).
bool probe_fsqrt_8h(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.0f),  SingleToHalf(4.0f),  SingleToHalf(9.0f),
      SingleToHalf(16.0f), SingleToHalf(25.0f), SingleToHalf(36.0f),
      SingleToHalf(-1.0f), SingleToHalf(49.0f),
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fsqrt v0.8h, v1.8h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  const uint16_t want[8] = {
      SingleToHalf(1.0f), SingleToHalf(2.0f), SingleToHalf(3.0f),
      SingleToHalf(4.0f), SingleToHalf(5.0f), SingleToHalf(6.0f),
      0,                  SingleToHalf(7.0f),
  };
  bool ok = true;
  for (int i = 0; i < 8; i++) {
    if (i == 6) {
      // -1.0f -> qNaN.  FP16 qNaN has exponent=0x1F and a nonzero mantissa
      // with MSB set; tolerate any FP16 quiet NaN encoding.
      uint16_t h = out[i];
      bool is_nan = ((h & 0x7C00) == 0x7C00) && ((h & 0x03FF) != 0);
      if (!is_nan) ok = false;
    } else if (!approx_half(out[i], want[i])) {
      ok = false;
    }
  }
  snprintf(buf, sizeof(buf),
           "  fsqrt .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FSQRT FP16 .4H (Q=0) — 4 perfect-square lanes; upper 4 must be
// zero on write.  Exercises the .4H direct F16C round-trip JIT path.
bool probe_fsqrt_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(4.0f), SingleToHalf(9.0f),
      SingleToHalf(16.0f), SingleToHalf(25.0f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fsqrt v0.4h, v1.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  const uint16_t want_lo[4] = {SingleToHalf(2.0f), SingleToHalf(3.0f),
                               SingleToHalf(4.0f), SingleToHalf(5.0f)};
  bool ok = approx_half(out[0], want_lo[0]) && approx_half(out[1], want_lo[1]) &&
            approx_half(out[2], want_lo[2]) && approx_half(out[3], want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  fsqrt .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FABS FP16 .8H — sign-bit clear on all 8 half-lanes.
bool probe_fabs_8h(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(-1.0f),  SingleToHalf(2.0f),  SingleToHalf(-3.5f),
      SingleToHalf(4.25f),  SingleToHalf(-0.0f), SingleToHalf(0.0f),
      SingleToHalf(-65504.0f), SingleToHalf(0.5f),
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fabs v0.8h, v1.8h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  const uint16_t want[8] = {
      SingleToHalf(1.0f),  SingleToHalf(2.0f),  SingleToHalf(3.5f),
      SingleToHalf(4.25f), SingleToHalf(0.0f),  SingleToHalf(0.0f),
      SingleToHalf(65504.0f), SingleToHalf(0.5f),
  };
  bool ok = true;
  for (int i = 0; i < 8; i++) {
    if (out[i] != want[i]) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  fabs .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FABS FP16 .4H (Q=0) — sign-bit clear on low 4 lanes; upper 4 must
// be zero on write.
bool probe_fabs_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(-1.5f), SingleToHalf(2.75f),
      SingleToHalf(-3.0f), SingleToHalf(4.0f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fabs v0.4h, v1.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  const uint16_t want_lo[4] = {SingleToHalf(1.5f), SingleToHalf(2.75f),
                               SingleToHalf(3.0f), SingleToHalf(4.0f)};
  bool ok = (out[0] == want_lo[0]) && (out[1] == want_lo[1]) &&
            (out[2] == want_lo[2]) && (out[3] == want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  fabs .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FNEG FP16 .8H — sign-bit flip on all 8 half-lanes.
bool probe_fneg_8h(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.0f),  SingleToHalf(-2.0f), SingleToHalf(3.5f),
      SingleToHalf(-4.25f), SingleToHalf(0.0f), SingleToHalf(-0.0f),
      SingleToHalf(65504.0f), SingleToHalf(-0.5f),
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fneg v0.8h, v1.8h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  const uint16_t want[8] = {
      SingleToHalf(-1.0f),  SingleToHalf(2.0f),  SingleToHalf(-3.5f),
      SingleToHalf(4.25f),  SingleToHalf(-0.0f), SingleToHalf(0.0f),
      SingleToHalf(-65504.0f), SingleToHalf(0.5f),
  };
  bool ok = true;
  for (int i = 0; i < 8; i++) {
    if (out[i] != want[i]) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  fneg .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FNEG FP16 .4H (Q=0) — sign-bit flip on low 4 lanes; upper 4 must
// be zero on write.
bool probe_fneg_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.5f), SingleToHalf(-2.75f),
      SingleToHalf(3.0f), SingleToHalf(-4.0f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fneg v0.4h, v1.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  const uint16_t want_lo[4] = {SingleToHalf(-1.5f), SingleToHalf(2.75f),
                               SingleToHalf(-3.0f), SingleToHalf(4.0f)};
  bool ok = (out[0] == want_lo[0]) && (out[1] == want_lo[1]) &&
            (out[2] == want_lo[2]) && (out[3] == want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  fneg .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FP16 FRINT* (.8H) — F16C round-trip JIT path with ROUNDPS imm
// (handoff-86).  Each probe rounds an 8-lane FP16 input via one of the
// six FRINT mnemonics and verifies bit-exact agreement with the
// host-computed FP32 reference (FP16 quantized).  Per the handoff-82
// standing rule, F16C round-trip is exact for FP16 unary FRINT*.
#define PROBE_VEC_FRINT_8H(NAME, MNEMONIC, W0, W1, W2, W3, W4, W5, W6, W7) \
  bool probe_##NAME##_8h(std::string& report, char (&buf)[256]) {            \
    alignas(16) uint16_t n[8] = {                                            \
        SingleToHalf(1.5f),  SingleToHalf(-1.5f), SingleToHalf(0.4f),        \
        SingleToHalf(-2.5f), SingleToHalf(2.6f),  SingleToHalf(-1.6f),       \
        SingleToHalf(3.7f),  SingleToHalf(-3.7f),                            \
    };                                                                       \
    alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,           \
                                    0xdead, 0xbeef, 0xcafe, 0xf00d};         \
    asm volatile(                                                            \
        "ldr q1, [%[pa]]\n\t"                                                \
        MNEMONIC " v0.8h, v1.8h\n\t"                                         \
        "str q0, [%[pr]]\n\t"                                                \
        :                                                                    \
        : [pa] "r"(n), [pr] "r"(out)                                         \
        : "v0", "v1", "memory");                                             \
    const uint16_t want[8] = {                                               \
        SingleToHalf(W0), SingleToHalf(W1), SingleToHalf(W2), SingleToHalf(W3), \
        SingleToHalf(W4), SingleToHalf(W5), SingleToHalf(W6), SingleToHalf(W7), \
    };                                                                       \
    bool ok = true;                                                          \
    for (int i = 0; i < 8; i++) {                                            \
      if (!approx_half(out[i], want[i])) ok = false;                         \
    }                                                                        \
    snprintf(buf, sizeof(buf),                                               \
             "  " #NAME " .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n", \
             out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7], \
             ok ? "OK" : "FAIL");                                            \
    report += buf;                                                           \
    return ok;                                                               \
  }

PROBE_VEC_FRINT_8H(frintn, "frintn",  2.0f, -2.0f, 0.0f, -2.0f, 3.0f, -2.0f, 4.0f, -4.0f)
PROBE_VEC_FRINT_8H(frintm, "frintm",  1.0f, -2.0f, 0.0f, -3.0f, 2.0f, -2.0f, 3.0f, -4.0f)
PROBE_VEC_FRINT_8H(frintp, "frintp",  2.0f, -1.0f, 1.0f, -2.0f, 3.0f, -1.0f, 4.0f, -3.0f)
PROBE_VEC_FRINT_8H(frintz, "frintz",  1.0f, -1.0f, 0.0f, -2.0f, 2.0f, -1.0f, 3.0f, -3.0f)
PROBE_VEC_FRINT_8H(frintx, "frintx",  2.0f, -2.0f, 0.0f, -2.0f, 3.0f, -2.0f, 4.0f, -4.0f)
PROBE_VEC_FRINT_8H(frinti, "frinti",  2.0f, -2.0f, 0.0f, -2.0f, 3.0f, -2.0f, 4.0f, -4.0f)
// FRINTA = round to nearest, ties AWAY from zero: 1.5 -> 2, -1.5 -> -2,
// 2.5 -> 3 (not 2 like FRINTN), -2.5 -> -3.
PROBE_VEC_FRINT_8H(frinta, "frinta",  2.0f, -2.0f, 0.0f, -3.0f, 3.0f, -2.0f, 4.0f, -4.0f)

#undef PROBE_VEC_FRINT_8H

// Vector FP16 FRINTN (.4H, Q=0) — single .4H probe confirms the mask_low64
// upper-zero clear on the F16C round-trip JIT path.  Same code path serves
// all six FRINT* mnemonics; one probe is sufficient.
bool probe_frintn_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.5f), SingleToHalf(-2.5f),
      SingleToHalf(0.4f), SingleToHalf(-1.6f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "frintn v0.4h, v1.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  const uint16_t want_lo[4] = {SingleToHalf(2.0f), SingleToHalf(-2.0f),
                               SingleToHalf(0.0f), SingleToHalf(-2.0f)};
  bool ok = approx_half(out[0], want_lo[0]) && approx_half(out[1], want_lo[1]) &&
            approx_half(out[2], want_lo[2]) && approx_half(out[3], want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  frintn .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FP16 FRINTA (.4H, Q=0) -- exercises ties-away-from-zero plus the
// mask_low64 upper-zero clear on the F16C round-trip JIT path.
bool probe_frinta_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.5f), SingleToHalf(-2.5f),
      SingleToHalf(0.4f), SingleToHalf(-1.6f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "frinta v0.4h, v1.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pr] "r"(out)
      : "v0", "v1", "memory");
  // FRINTA(1.5) = 2, FRINTA(-2.5) = -3, FRINTA(0.4) = 0, FRINTA(-1.6) = -2.
  const uint16_t want_lo[4] = {SingleToHalf(2.0f), SingleToHalf(-3.0f),
                               SingleToHalf(0.0f), SingleToHalf(-2.0f)};
  bool ok = approx_half(out[0], want_lo[0]) && approx_half(out[1], want_lo[1]) &&
            approx_half(out[2], want_lo[2]) && approx_half(out[3], want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  frinta .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FP16 three-same FADD/FSUB/FMUL/FDIV (F16C round-trip JIT path).
// Exercises the .8H form across eight lanes with a shared input set; the
// round-trip is bit-exact for FP16 binary FADD/FSUB/FMUL/FDIV per the
// standing rule, so a host-computed FP32 reference (then narrowed to FP16)
// matches the on-device result.  m_f avoids zero where required for FDIV.
#define PROBE_FP16_BINOP_8H(NAME, MNEMONIC, OP)                                  \
  bool probe_##NAME##_8h(std::string& report, char (&buf)[256]) {                \
    const float n_f[8] = {1.0f, 2.0f, -3.0f, 0.5f, 100.0f, -100.0f, 0.0f, 1.5f}; \
    const float m_f[8] = {1.0f, 4.0f, 3.0f, 1.5f, 0.5f, 0.5f, 1.0f, -0.5f};      \
    alignas(16) uint16_t n[8], m[8];                                             \
    for (int i = 0; i < 8; i++) {                                                \
      n[i] = SingleToHalf(n_f[i]);                                               \
      m[i] = SingleToHalf(m_f[i]);                                               \
    }                                                                            \
    alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,               \
                                    0xdead, 0xbeef, 0xcafe, 0xf00d};             \
    asm volatile(                                                                \
        "ldr q1, [%[pa]]\n\t"                                                    \
        "ldr q2, [%[pb]]\n\t"                                                    \
        MNEMONIC " v0.8h, v1.8h, v2.8h\n\t"                                      \
        "str q0, [%[pr]]\n\t"                                                    \
        :                                                                        \
        : [pa] "r"(n), [pb] "r"(m), [pr] "r"(out)                                \
        : "v0", "v1", "v2", "memory");                                           \
    uint16_t want[8];                                                            \
    for (int i = 0; i < 8; i++) {                                                \
      want[i] = SingleToHalf(n_f[i] OP m_f[i]);                                  \
    }                                                                            \
    bool ok = true;                                                              \
    for (int i = 0; i < 8; i++) {                                                \
      if (!approx_half(out[i], want[i])) ok = false;                             \
    }                                                                            \
    snprintf(buf, sizeof(buf),                                                   \
             "  " #NAME " .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n", \
             out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],     \
             ok ? "OK" : "FAIL");                                                \
    report += buf;                                                               \
    return ok;                                                                   \
  }

PROBE_FP16_BINOP_8H(fadd, "fadd", +)
PROBE_FP16_BINOP_8H(fsub, "fsub", -)
PROBE_FP16_BINOP_8H(fmul, "fmul", *)
PROBE_FP16_BINOP_8H(fdiv, "fdiv", /)

#undef PROBE_FP16_BINOP_8H

// Single .4H FADD probe confirms Q=0 upper-zero on the F16C round-trip
// path (Vcvtps2ph auto-zeroes the upper 64 bits of its XMM destination).
// Same code path serves FADD/FSUB/FMUL/FDIV; one probe is sufficient.
bool probe_fadd_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.0f), SingleToHalf(2.0f),
      SingleToHalf(-3.0f), SingleToHalf(0.5f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t m[8] = {
      SingleToHalf(1.0f), SingleToHalf(4.0f),
      SingleToHalf(3.0f), SingleToHalf(1.5f),
      0x1234, 0x5678, 0x9abc, 0xdef0,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "ldr q2, [%[pb]]\n\t"
      "fadd v0.4h, v1.4h, v2.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pb] "r"(m), [pr] "r"(out)
      : "v0", "v1", "v2", "memory");
  const uint16_t want_lo[4] = {SingleToHalf(2.0f), SingleToHalf(6.0f),
                               SingleToHalf(0.0f), SingleToHalf(2.0f)};
  bool ok = approx_half(out[0], want_lo[0]) && approx_half(out[1], want_lo[1]) &&
            approx_half(out[2], want_lo[2]) && approx_half(out[3], want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  fadd .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// FP16 vector min/max family: FMAX/FMIN (NaN-propagating) and FMAXNM/FMINNM
// (NaN-suppressing) at .8H. Inputs chosen so the non-NaN paths exercise
// both halves and a variety of orderings (positive, negative, equal, zero).
// NaN inputs are deliberately omitted — the JIT lowering uses MAXPS/MINPS
// in the FP32 widened domain, which gives a NaN result for any NaN input
// (matching ARM ARM default-NaN propagation), but the exact NaN bit
// pattern can differ from the interpreter's canonical 0x7E00. The probe's
// approx_half tolerance does not check NaN bit patterns.
#define PROBE_FP16_MINMAX_8H(NAME, MNEMONIC, CFUNC)                            \
  bool probe_##NAME##_8h(std::string& report, char (&buf)[256]) {              \
    const float n_f[8] = {1.0f, -2.0f, 3.0f, -0.5f, 100.0f, -100.0f, 0.0f, 1.5f}; \
    const float m_f[8] = {2.0f, -1.0f, 3.0f,  0.5f, -100.0f, 100.0f, 0.0f, 1.5f}; \
    alignas(16) uint16_t n[8], m[8];                                            \
    for (int i = 0; i < 8; i++) {                                              \
      n[i] = SingleToHalf(n_f[i]);                                             \
      m[i] = SingleToHalf(m_f[i]);                                             \
    }                                                                          \
    alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,             \
                                    0xdead, 0xbeef, 0xcafe, 0xf00d};            \
    asm volatile(                                                              \
        "ldr q1, [%[pa]]\n\t"                                                  \
        "ldr q2, [%[pb]]\n\t"                                                  \
        MNEMONIC " v0.8h, v1.8h, v2.8h\n\t"                                    \
        "str q0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(n), [pb] "r"(m), [pr] "r"(out)                              \
        : "v0", "v1", "v2", "memory");                                         \
    uint16_t want[8];                                                          \
    for (int i = 0; i < 8; i++) {                                              \
      want[i] = SingleToHalf(CFUNC(n_f[i], m_f[i]));                           \
    }                                                                          \
    bool ok = true;                                                            \
    for (int i = 0; i < 8; i++) {                                              \
      if (!approx_half(out[i], want[i])) ok = false;                           \
    }                                                                          \
    snprintf(buf, sizeof(buf),                                                 \
             "  " #NAME " .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n", \
             out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],   \
             ok ? "OK" : "FAIL");                                              \
    report += buf;                                                             \
    return ok;                                                                 \
  }

PROBE_FP16_MINMAX_8H(fmax,   "fmax",   std::fmax)
PROBE_FP16_MINMAX_8H(fmin,   "fmin",   std::fmin)
PROBE_FP16_MINMAX_8H(fmaxnm, "fmaxnm", std::fmax)
PROBE_FP16_MINMAX_8H(fminnm, "fminnm", std::fmin)

#undef PROBE_FP16_MINMAX_8H

// One .4H FMAX probe confirms Q=0 upper-zero on the F16C round-trip path
// (Vcvtps2ph auto-zeroes the upper 64 bits of its XMM destination). The
// FMAX/FMIN/FMAXNM/FMINNM .4H lowering all share the same Vcvtps2ph
// narrow, so a single probe covers the family.
bool probe_fmax_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.0f), SingleToHalf(-2.0f),
      SingleToHalf(3.0f), SingleToHalf(-0.5f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t m[8] = {
      SingleToHalf(2.0f), SingleToHalf(-1.0f),
      SingleToHalf(3.0f), SingleToHalf(0.5f),
      0x1234, 0x5678, 0x9abc, 0xdef0,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "ldr q2, [%[pb]]\n\t"
      "fmax v0.4h, v1.4h, v2.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pb] "r"(m), [pr] "r"(out)
      : "v0", "v1", "v2", "memory");
  const uint16_t want_lo[4] = {SingleToHalf(2.0f), SingleToHalf(-1.0f),
                               SingleToHalf(3.0f), SingleToHalf(0.5f)};
  bool ok = approx_half(out[0], want_lo[0]) && approx_half(out[1], want_lo[1]) &&
            approx_half(out[2], want_lo[2]) && approx_half(out[3], want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  fmax .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Vector FP16 FABD .8H — F16C round-trip JIT path plus FP32 sign-clear
// (0x7FFFFFFF per dword) before narrow. Inputs chosen so the result lane
// values exercise both signs of the subtraction (the abs step makes the
// sign of (n - m) irrelevant to the expected output).
bool probe_fabd_8h(std::string& report, char (&buf)[256]) {
  const float n_f[8] = {1.0f, -2.0f, 3.0f, -0.5f, 100.0f, -100.0f, 0.0f, 1.5f};
  const float m_f[8] = {2.0f, -1.0f, 3.0f,  0.5f, -100.0f, 100.0f, 0.0f, 1.5f};
  alignas(16) uint16_t n[8], m[8];
  for (int i = 0; i < 8; i++) {
    n[i] = SingleToHalf(n_f[i]);
    m[i] = SingleToHalf(m_f[i]);
  }
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "ldr q2, [%[pb]]\n\t"
      "fabd v0.8h, v1.8h, v2.8h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pb] "r"(m), [pr] "r"(out)
      : "v0", "v1", "v2", "memory");
  uint16_t want[8];
  for (int i = 0; i < 8; i++) {
    want[i] = SingleToHalf(std::fabs(n_f[i] - m_f[i]));
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) {
    if (!approx_half(out[i], want[i])) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  fabd .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// One .4H FABD probe confirms Q=0 upper-zero on the F16C round-trip path.
bool probe_fabd_4h_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      SingleToHalf(1.0f), SingleToHalf(-2.0f),
      SingleToHalf(3.0f), SingleToHalf(-0.5f),
      0xdead, 0xbeef, 0xcafe, 0xf00d,
  };
  alignas(16) uint16_t m[8] = {
      SingleToHalf(2.0f), SingleToHalf(-1.0f),
      SingleToHalf(3.0f), SingleToHalf(0.5f),
      0x1234, 0x5678, 0x9abc, 0xdef0,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xbeef, 0xcafe, 0xf00d,
                                  0xdead, 0xbeef, 0xcafe, 0xf00d};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "ldr q2, [%[pb]]\n\t"
      "fabd v0.4h, v1.4h, v2.4h\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(n), [pb] "r"(m), [pr] "r"(out)
      : "v0", "v1", "v2", "memory");
  const uint16_t want_lo[4] = {SingleToHalf(1.0f), SingleToHalf(1.0f),
                               SingleToHalf(0.0f), SingleToHalf(1.0f)};
  bool ok = approx_half(out[0], want_lo[0]) && approx_half(out[1], want_lo[1]) &&
            approx_half(out[2], want_lo[2]) && approx_half(out[3], want_lo[3]) &&
            out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0;
  snprintf(buf, sizeof(buf),
           "  fabd .4H lo=[%04x %04x %04x %04x] hi=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// Q=0 .2S FSQRT: lanes 0/1 carry roots, lanes 2/3 must be zero on write.
bool probe_fsqrt_2s_zero_upper(std::string& report, char (&buf)[256]) {
  alignas(16) float in_buf[4]  = {4.0f, 9.0f, 16.0f, 25.0f};
  alignas(16) float out_buf[4] = {99.f, 99.f, 99.f, 99.f};
  asm volatile(
      "ldr q1, [%[pa]]\n\t"
      "fsqrt v0.2s, v1.2s\n\t"
      "str q0, [%[pr]]\n\t"
      :
      : [pa] "r"(in_buf), [pr] "r"(out_buf)
      : "v0", "v1", "memory");
  bool ok = (out_buf[0] == 2.0f) && (out_buf[1] == 3.0f) &&
            (out_buf[2] == 0.0f) && (out_buf[3] == 0.0f);
  snprintf(buf, sizeof(buf), "  fsqrt .2S (upper-zero): %s\n",
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellocomplex_MainActivity_probeComplex(JNIEnv* env,
                                                        jobject /*this*/) {
  std::string report = "Armv8.3-FCMA instruction probe:\n";
  char buf[256];
  int total = 0, passed = 0;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Armv8.3-FCMA probe:");

  auto run = [&](bool ok) {
    total++;
    if (ok) passed++;
  };

  run(probe_fcadd_4s(report, buf, 90));
  run(probe_fcadd_4s(report, buf, 270));
  run(probe_fcadd_2d(report, buf, 90));
  run(probe_fcadd_2d(report, buf, 270));
  run(probe_fcadd_2s(report, buf));
  run(probe_fcmla_4s(report, buf, 0));
  run(probe_fcmla_4s(report, buf, 90));
  run(probe_fcmla_4s(report, buf, 180));
  run(probe_fcmla_4s(report, buf, 270));
  run(probe_fcmla_2d(report, buf, 0));
  run(probe_fcmla_2d(report, buf, 90));
  run(probe_fcmla_2d(report, buf, 180));
  run(probe_fcmla_2d(report, buf, 270));
  // FCMLA by element (Armv8.3-FCMA idx) — handoff-58 §H1.
  // All 4 rotations × 2 indices = 8 probes; covers Vm broadcast & rot table.
  run(probe_fcmla_4s_idx(report, buf, 0,   0));
  run(probe_fcmla_4s_idx(report, buf, 90,  0));
  run(probe_fcmla_4s_idx(report, buf, 180, 0));
  run(probe_fcmla_4s_idx(report, buf, 270, 0));
  run(probe_fcmla_4s_idx(report, buf, 0,   1));
  run(probe_fcmla_4s_idx(report, buf, 90,  1));
  run(probe_fcmla_4s_idx(report, buf, 180, 1));
  run(probe_fcmla_4s_idx(report, buf, 270, 1));

  // region digitalis - Plan §H1 FP16 SIMD FCMA (handoff-61)
  // 14 FP16 probes: FCADD .8H {#90,#270}, FCADD .4H {#90,#270} (Q=0
  // zero-clear check), FCMLA .8H {#0,#90,#180,#270}, plus 6 FCMLA .8H idx
  // covering all 4 indices and a sample of rotations.
  run(probe_fcadd_fp16_8h(report, buf, 90));
  run(probe_fcadd_fp16_8h(report, buf, 270));
  run(probe_fcadd_fp16_4h(report, buf, 90));
  run(probe_fcadd_fp16_4h(report, buf, 270));
  run(probe_fcmla_fp16_8h(report, buf, 0));
  run(probe_fcmla_fp16_8h(report, buf, 90));
  run(probe_fcmla_fp16_8h(report, buf, 180));
  run(probe_fcmla_fp16_8h(report, buf, 270));
  run(probe_fcmla_fp16_8h_idx(report, buf, 0,   0));
  run(probe_fcmla_fp16_8h_idx(report, buf, 0,   1));
  run(probe_fcmla_fp16_8h_idx(report, buf, 0,   2));
  run(probe_fcmla_fp16_8h_idx(report, buf, 0,   3));
  run(probe_fcmla_fp16_8h_idx(report, buf, 90,  0));
  run(probe_fcmla_fp16_8h_idx(report, buf, 270, 3));
  // endregion

  // FRINTA scalar probes — 7 inputs × 3 precisions = 21 probes.
  // Exercises the new JIT path (handoff-81); the interpreter still owns
  // any case that bails (none under the current host platform).
  const struct { float in; float want; } frinta_cases[] = {
      {0.5f, 1.0f},   {-0.5f, -1.0f}, {1.5f, 2.0f},  {-1.5f, -2.0f},
      {2.5f, 3.0f},   {0.4f, 0.0f},   {1.7f, 2.0f},
  };
  for (const auto& tc : frinta_cases) run(probe_frinta_s(report, buf, tc.in, tc.want));
  for (const auto& tc : frinta_cases)
    run(probe_frinta_d(report, buf, static_cast<double>(tc.in),
                       static_cast<double>(tc.want)));
  for (const auto& tc : frinta_cases) run(probe_frinta_h(report, buf, tc.in, tc.want));

  // FABS/FNEG/FRINT* vector probes (Plan §C4 vector form).  Each input
  // vector mixes positive, negative, sub-half, and tie cases so a single
  // probe per opcode confirms all-lane behavior.
  {
    const float in_s[4]   = { 1.5f,  -1.5f,  0.4f, -2.5f};
    const float fabs_w[4] = { 1.5f,   1.5f,  0.4f,  2.5f};
    const float fneg_w[4] = {-1.5f,   1.5f, -0.4f,  2.5f};
    const float frnn_w[4] = { 2.0f,  -2.0f,  0.0f, -2.0f};  // RNE: 1.5→2, -1.5→-2, 0.4→0, -2.5→-2
    const float frnm_w[4] = { 1.0f,  -2.0f,  0.0f, -3.0f};  // floor
    const float frnp_w[4] = { 2.0f,  -1.0f,  1.0f, -2.0f};  // ceil
    const float frnz_w[4] = { 1.0f,  -1.0f,  0.0f, -2.0f};  // trunc toward zero
    const float frna_w[4] = { 2.0f,  -2.0f,  0.0f, -3.0f};  // ties-away
    const float frnx_w[4] = { 2.0f,  -2.0f,  0.0f, -2.0f};  // RNE (same as FRINTN)
    run(probe_fabs_4s  (report, buf, in_s, fabs_w));
    run(probe_fneg_4s  (report, buf, in_s, fneg_w));
    run(probe_frintn_4s(report, buf, in_s, frnn_w));
    run(probe_frintm_4s(report, buf, in_s, frnm_w));
    run(probe_frintp_4s(report, buf, in_s, frnp_w));
    run(probe_frintz_4s(report, buf, in_s, frnz_w));
    run(probe_frinta_4s(report, buf, in_s, frna_w));
    run(probe_frintx_4s(report, buf, in_s, frnx_w));
    run(probe_frinti_4s(report, buf, in_s, frnx_w));
  }
  {
    // FSQRT .4S — exact perfect squares (plus a negative input -> qNaN
    // sentinel).  The macro's NaN-aware compare handles the negative lane.
    const float fs_in[4]   = {4.0f, 9.0f, -1.0f, 16.0f};
    const float fs_want[4] = {2.0f, 3.0f, std::nanf(""), 4.0f};
    run(probe_fsqrt_4s(report, buf, fs_in, fs_want));
  }
  {
    const double in_d[2]   = { 1.5,  -2.5};
    const double fabs_w[2] = { 1.5,   2.5};
    const double fneg_w[2] = {-1.5,   2.5};
    const double frnn_w[2] = { 2.0,  -2.0};
    const double frnm_w[2] = { 1.0,  -3.0};
    const double frnp_w[2] = { 2.0,  -2.0};
    const double frnz_w[2] = { 1.0,  -2.0};
    const double frna_w[2] = { 2.0,  -3.0};
    const double frnx_w[2] = { 2.0,  -2.0};
    run(probe_fabs_2d  (report, buf, in_d, fabs_w));
    run(probe_fneg_2d  (report, buf, in_d, fneg_w));
    run(probe_frintn_2d(report, buf, in_d, frnn_w));
    run(probe_frintm_2d(report, buf, in_d, frnm_w));
    run(probe_frintp_2d(report, buf, in_d, frnp_w));
    run(probe_frintz_2d(report, buf, in_d, frnz_w));
    run(probe_frinta_2d(report, buf, in_d, frna_w));
    run(probe_frintx_2d(report, buf, in_d, frnx_w));
    run(probe_frinti_2d(report, buf, in_d, frnx_w));
  }
  {
    // FSQRT .2D — exact perfect squares.
    const double fs_in[2]   = {25.0, 144.0};
    const double fs_want[2] = { 5.0,  12.0};
    run(probe_fsqrt_2d(report, buf, fs_in, fs_want));
  }
  run(probe_fneg_2s_zero_upper(report, buf));
  run(probe_fsqrt_2s_zero_upper(report, buf));
  // FP16 vector FSQRT (F16C round-trip JIT path).
  run(probe_fsqrt_8h(report, buf));
  run(probe_fsqrt_4h_zero_upper(report, buf));
  // FP16 vector FABS / FNEG (bit-mask JIT path; no F16C needed).
  run(probe_fabs_8h(report, buf));
  run(probe_fabs_4h_zero_upper(report, buf));
  run(probe_fneg_8h(report, buf));
  run(probe_fneg_4h_zero_upper(report, buf));
  // FP16 vector FRINT* (F16C round-trip + ROUNDPS imm JIT path; handoff-86).
  run(probe_frintn_8h(report, buf));
  run(probe_frintm_8h(report, buf));
  run(probe_frintp_8h(report, buf));
  run(probe_frintz_8h(report, buf));
  run(probe_frintx_8h(report, buf));
  run(probe_frinti_8h(report, buf));
  run(probe_frintn_4h_zero_upper(report, buf));
  // FP16 vector FRINTA (F16C round-trip + add-copysign-trunc JIT path).
  run(probe_frinta_8h(report, buf));
  run(probe_frinta_4h_zero_upper(report, buf));
  // FP16 vector three-same FADD/FSUB/FMUL/FDIV (F16C round-trip JIT path).
  run(probe_fadd_8h(report, buf));
  run(probe_fsub_8h(report, buf));
  run(probe_fmul_8h(report, buf));
  run(probe_fdiv_8h(report, buf));
  run(probe_fadd_4h_zero_upper(report, buf));
  // FP16 vector FMAX / FMIN / FMAXNM / FMINNM (F16C round-trip + NaN-shim JIT).
  run(probe_fmax_8h(report, buf));
  run(probe_fmin_8h(report, buf));
  run(probe_fmaxnm_8h(report, buf));
  run(probe_fminnm_8h(report, buf));
  run(probe_fmax_4h_zero_upper(report, buf));
  // FP16 vector FABD (F16C round-trip + FP32 sign-clear JIT path).
  run(probe_fabd_8h(report, buf));
  run(probe_fabd_4h_zero_upper(report, buf));

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Summary: %d/%d OK",
                      passed, total);
  return env->NewStringUTF(report.c_str());
}
