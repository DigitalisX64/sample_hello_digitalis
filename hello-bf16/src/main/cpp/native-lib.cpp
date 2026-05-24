// hello-bf16: integration-level probe for Armv8.6-BF16 (§H2 / §M1).
//
// Probes every BFloat16 encoding implemented by the interpreter:
//
//   BFCVT  (scalar)              FP32 -> BF16 RTNE
//   BFCVTN  vec  .4h, .4s        FP32x4 -> BF16x4 low
//   BFCVTN2 vec  .8h, .4s        FP32x4 -> BF16x4 high
//   BFDOT  vec   .4s, .8h, .8h   per-lane dot(BF16 pair)
//   BFDOT  idx   .4s, .8h, .2h[i] broadcast BF16 pair
//   BFMMLA       .4s, .8h, .8h   2x2 = 2x4 . (2x4)^T
//   BFMLALB vec  .4s, .8h, .8h   low half widening MAC
//   BFMLALT vec  .4s, .8h, .8h   high half widening MAC
//   BFMLALB idx  .4s, .8h, .h[i] broadcast BF16 lane (B)
//   BFMLALT idx  .4s, .8h, .h[i] broadcast BF16 lane (T)
//
// Each probe drives one inline-asm instruction with stack-allocated
// 16-byte aligned input/output buffers, then computes the expected
// result in portable C using a shift-widen (BF16 -> FP32 is the BF16
// pattern << 16) and RTNE narrow (FP32 -> BF16 is round-to-nearest-even
// with NaN quieting).  If Digitalis routes any BF16 encoding to
// Undefined() the entire native lib SIGILLs on the first call.

#include <android/log.h>
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "hellobf16"

namespace {

// BF16 helpers — these mirror the interpreter helpers.

inline float bf2f(uint16_t bf) {
  uint32_t u = static_cast<uint32_t>(bf) << 16;
  float f;
  std::memcpy(&f, &u, sizeof(f));
  return f;
}

inline uint16_t f2bf(float f) {
  uint32_t u;
  std::memcpy(&u, &f, sizeof(u));
  // NaN: force quiet (top mantissa bit set).
  if ((u & 0x7fffffffu) > 0x7f800000u) {
    return static_cast<uint16_t>((u >> 16) | 0x0040u);
  }
  // RTNE: add 0x7fff + (mantissa LSB after truncation), then shift.
  uint32_t lsb = (u >> 16) & 1u;
  uint32_t rounded = u + 0x7fffu + lsb;
  return static_cast<uint16_t>(rounded >> 16);
}

bool approx(float got, float want) {
  float tol = 1e-2f * std::fabs(want) + 1e-3f;
  return std::fabs(got - want) <= tol;
}

bool probe_bfcvt_scalar(std::string& report, char (&buf)[256]) {
  // BFCVT  Hd, Sn — convert one FP32 to one BF16. Encoding 0x1e634000
  // is `bfcvt h0, s0`.
  const float inputs[] = {1.0f, -2.5f, 3.1415927f, 0.0f, -0.0f, 1e30f};
  bool all_ok = true;
  for (float f : inputs) {
    uint32_t in_bits;
    std::memcpy(&in_bits, &f, sizeof(in_bits));
    uint32_t got = 0;
    __asm__ __volatile__(
        "fmov s0, %w1\n"
        ".inst 0x1e634000  // bfcvt h0, s0\n"
        "fmov %w0, s0\n"
        : "=r"(got)
        : "r"(in_bits)
        : "v0");
    uint16_t got_bf = static_cast<uint16_t>(got & 0xffffu);
    uint16_t want = f2bf(f);
    bool ok = (got_bf == want);
    if (!ok) all_ok = false;
    snprintf(buf, sizeof(buf), "  BFCVT scalar  f=%-12g -> bf16=0x%04x, want 0x%04x: %s\n",
             static_cast<double>(f), got_bf, want, ok ? "OK" : "FAIL");
    report += buf;
  }
  return all_ok;
}

bool probe_bfcvtn(std::string& report, char (&buf)[256]) {
  // BFCVTN  Vd.4h, Vn.4s — narrow 4 FP32 -> 4 BF16 (low half), upper half zero.
  alignas(16) float in[4] = {1.0f, -2.5f, 3.1415927f, 100.5f};
  alignas(16) uint16_t out[8] = {0xdead, 0xdead, 0xdead, 0xdead,
                                  0xdead, 0xdead, 0xdead, 0xdead};
  __asm__ __volatile__(
      "ldr q1, [%0]\n"
      ".inst 0x0ea16820  // bfcvtn v0.4h, v1.4s\n"
      "str q0, [%1]\n"
      :
      : "r"(in), "r"(out)
      : "v0", "v1", "memory");
  bool ok = true;
  for (int i = 0; i < 4; i++) {
    if (out[i] != f2bf(in[i])) { ok = false; break; }
  }
  for (int i = 4; i < 8; i++) {
    if (out[i] != 0) { ok = false; break; }
  }
  snprintf(buf, sizeof(buf),
           "  BFCVTN vec    low=[%04x %04x %04x %04x] high=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_bfcvtn2(std::string& report, char (&buf)[256]) {
  // BFCVTN2 Vd.8h, Vn.4s — narrow 4 FP32 -> 4 BF16 (high half), low half preserved.
  alignas(16) float in[4] = {-1.0f, 2.0f, -3.0f, 4.0f};
  alignas(16) uint16_t out[8] = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd,
                                  0xeeee, 0xffff, 0x1234, 0x5678};
  __asm__ __volatile__(
      "ldr q1, [%0]\n"
      "ldr q0, [%1]\n"
      ".inst 0x4ea16820  // bfcvtn2 v0.8h, v1.4s\n"
      "str q0, [%1]\n"
      :
      : "r"(in), "r"(out)
      : "v0", "v1", "memory");
  bool ok = true;
  uint16_t want_low[4] = {0xaaaa, 0xbbbb, 0xcccc, 0xdddd};
  for (int i = 0; i < 4; i++) {
    if (out[i] != want_low[i]) { ok = false; break; }
  }
  for (int i = 0; i < 4; i++) {
    if (out[4 + i] != f2bf(in[i])) { ok = false; break; }
  }
  snprintf(buf, sizeof(buf),
           "  BFCVTN2 vec   low=[%04x %04x %04x %04x] high=[%04x %04x %04x %04x]: %s\n",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_bfdot_vec(std::string& report, char (&buf)[256]) {
  // BFDOT  Vd.4s, Vn.8h, Vm.8h — per output lane, accumulate dot product of
  // two BF16 pairs.  Vd is read-modify-write.
  alignas(16) uint16_t n[8], m[8];
  float src_n[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float src_m[8] = {0.5f, 0.25f, 0.125f, 0.0625f, 1.0f, 1.0f, 1.0f, 1.0f};
  for (int i = 0; i < 8; i++) {
    n[i] = f2bf(src_n[i]);
    m[i] = f2bf(src_m[i]);
  }
  alignas(16) float pre[4] = {10.0f, 20.0f, 30.0f, 40.0f};
  alignas(16) float out[4];
  std::memcpy(out, pre, 16);
  __asm__ __volatile__(
      "ldr q1, [%0]\n"
      "ldr q2, [%1]\n"
      "ldr q0, [%2]\n"
      ".inst 0x6e42fc20  // bfdot v0.4s, v1.8h, v2.8h\n"
      "str q0, [%2]\n"
      :
      : "r"(n), "r"(m), "r"(out)
      : "v0", "v1", "v2", "memory");
  bool ok = true;
  float want[4];
  for (int i = 0; i < 4; i++) {
    want[i] = pre[i] + bf2f(n[2 * i]) * bf2f(m[2 * i])
                      + bf2f(n[2 * i + 1]) * bf2f(m[2 * i + 1]);
    if (!approx(out[i], want[i])) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  BFDOT vec     out=[%.4f %.4f %.4f %.4f] want=[%.4f %.4f %.4f %.4f]: %s\n",
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_bfdot_idx(std::string& report, char (&buf)[256]) {
  // BFDOT  Vd.4s, Vn.8h, Vm.2h[i] — broadcast one BF16 pair from Vm.
  // Pick index=1 -> Vm.h[2],[3]. Encoding bfdot v0.4s,v1.8h,v2.2h[1] = 0x4f62f020
  // (size=01, H=0, L=1, M=0, Vm=2).
  alignas(16) uint16_t n[8], m[8];
  float src_n[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float src_m[8] = {9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f};
  for (int i = 0; i < 8; i++) {
    n[i] = f2bf(src_n[i]);
    m[i] = f2bf(src_m[i]);
  }
  alignas(16) float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  __asm__ __volatile__(
      "ldr q1, [%0]\n"
      "ldr q2, [%1]\n"
      "ldr q0, [%2]\n"
      ".inst 0x4f62f020  // bfdot v0.4s, v1.8h, v2.2h[1]\n"
      "str q0, [%2]\n"
      :
      : "r"(n), "r"(m), "r"(out)
      : "v0", "v1", "v2", "memory");
  bool ok = true;
  float want[4];
  for (int i = 0; i < 4; i++) {
    want[i] = bf2f(n[2 * i]) * bf2f(m[2])
              + bf2f(n[2 * i + 1]) * bf2f(m[3]);
    if (!approx(out[i], want[i])) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  BFDOT idx[1]  out=[%.4f %.4f %.4f %.4f] want=[%.4f %.4f %.4f %.4f]: %s\n",
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_bfmmla(std::string& report, char (&buf)[256]) {
  // BFMMLA  Vd.4s, Vn.8h, Vm.8h — 2x2 matrix product into Vd.
  // Vd.s[2i+j] += Sum_k bf2f(Vn.h[4i+k]) * bf2f(Vm.h[4j+k]) for i,j in {0,1}.
  alignas(16) uint16_t n[8], m[8];
  float src_n[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float src_m[8] = {1.0f, 0.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  for (int i = 0; i < 8; i++) {
    n[i] = f2bf(src_n[i]);
    m[i] = f2bf(src_m[i]);
  }
  alignas(16) float pre[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  alignas(16) float out[4];
  std::memcpy(out, pre, 16);
  __asm__ __volatile__(
      "ldr q1, [%0]\n"
      "ldr q2, [%1]\n"
      "ldr q0, [%2]\n"
      ".inst 0x6e42ec20  // bfmmla v0.4s, v1.8h, v2.8h\n"
      "str q0, [%2]\n"
      :
      : "r"(n), "r"(m), "r"(out)
      : "v0", "v1", "v2", "memory");
  bool ok = true;
  float want[4];
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      float acc = pre[2 * i + j];
      for (int k = 0; k < 4; k++) {
        acc += bf2f(n[4 * i + k]) * bf2f(m[4 * j + k]);
      }
      want[2 * i + j] = acc;
    }
  }
  for (int i = 0; i < 4; i++) {
    if (!approx(out[i], want[i])) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  BFMMLA        out=[%.4f %.4f %.4f %.4f] want=[%.4f %.4f %.4f %.4f]: %s\n",
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_bfmlal_vec(std::string& report, char (&buf)[256], bool top) {
  // BFMLALB vec: bit30=0, opcode kBfmlalbVec, encoding 0x2ec2fc20.
  // BFMLALT vec: bit30=1, opcode kBfmlaltVec, encoding 0x6ec2fc20.
  alignas(16) uint16_t n[8], m[8];
  float src_n[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float src_m[8] = {0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f};
  for (int i = 0; i < 8; i++) {
    n[i] = f2bf(src_n[i]);
    m[i] = f2bf(src_m[i]);
  }
  alignas(16) float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  if (top) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6ec2fc20  // bfmlalt v0.4s, v1.8h, v2.8h\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x2ec2fc20  // bfmlalb v0.4s, v1.8h, v2.8h\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  }
  bool ok = true;
  float want[4];
  int off = top ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    want[i] = 0.0f + bf2f(n[2 * i + off]) * bf2f(m[2 * i + off]);
    if (!approx(out[i], want[i])) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  BFMLAL%c vec   out=[%.4f %.4f %.4f %.4f] want=[%.4f %.4f %.4f %.4f]: %s\n",
           top ? 'T' : 'B',
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

bool probe_bfmlal_idx(std::string& report, char (&buf)[256], bool top) {
  // BFMLAL indexed encoding uses index = (H << 2) | (L << 1) | M with:
  //   L at bit 21, M at bit 20, H at bit 11 (all three verified via llvm-mc).
  // Pick index=1 (M=1): bfmlalb v0.4s, v1.8h, v2.h[1] = 0x0fd2f020.
  // BFMLALT idx[1]: bit30=1 -> 0x4fd2f020.
  // Vm lane consumed is index 1 -> Vm.h[1].
  alignas(16) uint16_t n[8], m[8];
  float src_n[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
  float src_m[8] = {0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f};
  for (int i = 0; i < 8; i++) {
    n[i] = f2bf(src_n[i]);
    m[i] = f2bf(src_m[i]);
  }
  alignas(16) float out[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  if (top) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x4fd2f020  // bfmlalt v0.4s, v1.8h, v2.h[1]\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x0fd2f020  // bfmlalb v0.4s, v1.8h, v2.h[1]\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  }
  bool ok = true;
  float want[4];
  int off = top ? 1 : 0;
  for (int i = 0; i < 4; i++) {
    want[i] = 0.0f + bf2f(n[2 * i + off]) * bf2f(m[1]);
    if (!approx(out[i], want[i])) ok = false;
  }
  snprintf(buf, sizeof(buf),
           "  BFMLAL%c idx[1] out=[%.4f %.4f %.4f %.4f] want=[%.4f %.4f %.4f %.4f]: %s\n",
           top ? 'T' : 'B',
           static_cast<double>(out[0]), static_cast<double>(out[1]),
           static_cast<double>(out[2]), static_cast<double>(out[3]),
           static_cast<double>(want[0]), static_cast<double>(want[1]),
           static_cast<double>(want[2]), static_cast<double>(want[3]),
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellobf16_MainActivity_probeBf16(JNIEnv* env,
                                                  jobject /*this*/) {
  std::string report = "Armv8.6-BF16 instruction probe:\n";
  char buf[256];
  int total = 0, passed = 0;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Armv8.6-BF16 probe:");

  auto run = [&](bool ok) {
    total++;
    if (ok) passed++;
  };

  run(probe_bfcvt_scalar(report, buf));
  run(probe_bfcvtn(report, buf));
  run(probe_bfcvtn2(report, buf));
  run(probe_bfdot_vec(report, buf));
  run(probe_bfdot_idx(report, buf));
  run(probe_bfmmla(report, buf));
  run(probe_bfmlal_vec(report, buf, /*top=*/false));
  run(probe_bfmlal_vec(report, buf, /*top=*/true));
  run(probe_bfmlal_idx(report, buf, /*top=*/false));
  run(probe_bfmlal_idx(report, buf, /*top=*/true));

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Summary: %d/%d OK",
                      passed, total);
  return env->NewStringUTF(report.c_str());
}
