// hello-dotprod: integration-level probe for Armv8.4-DotProd (§C8 / §M1).
//
// Probes every SDOT/UDOT encoding the §C8 decoder + interpreter implement:
//
//   SDOT V.4S, Vn.16B, Vm.16B          signed,   4 lanes, vector form
//   UDOT V.4S, Vn.16B, Vm.16B          unsigned, 4 lanes, vector form
//   SDOT V.2S, Vn.8B,  Vm.8B           signed,   2 lanes, Q=0 (zeroes high half)
//   UDOT V.2S, Vn.8B,  Vm.8B           unsigned, 2 lanes, Q=0
//   SDOT V.4S, Vn.16B, Vm.4B[idx]      indexed, signed,   4 lanes
//   UDOT V.4S, Vn.16B, Vm.4B[idx]      indexed, unsigned, 4 lanes
//   SDOT V.2S, Vn.8B,  Vm.4B[idx]      indexed, signed,   Q=0
//   UDOT V.2S, Vn.8B,  Vm.4B[idx]      indexed, unsigned, Q=0
//
// Semantics: each 32-bit destination lane accumulates the dot product of 4
// byte products.  SDOT sign-extends both source byte vectors before
// multiplying; UDOT zero-extends.  Vd is read-modify-write (the lane is
// *accumulated*, not replaced).  Indexed form broadcasts a single 4-byte
// group from Vm.16B (selected by the 2-bit index) across all output lanes.
//
// The interpreter implementation is in interpreter.h::AdvSimdDotProduct
// (new this cycle).  If any DOT encoding routes to Undefined() the entire
// native lib SIGILLs on the first call.  If sign-extension is wrong, the
// SDOT probes that use negative bytes (we deliberately include them) will
// give the wrong result.  If the indexed broadcast picks the wrong group,
// the indexed probes will fail.
//
// llvm-mc-verified encodings (clang --target=aarch64 -march=armv8.4-a+dotprod):
//   0x4e829420  sdot v0.4s, v1.16b, v2.16b
//   0x6e829420  udot v0.4s, v1.16b, v2.16b
//   0x0e829420  sdot v0.2s, v1.8b,  v2.8b
//   0x2e829420  udot v0.2s, v1.8b,  v2.8b
//   0x4f82e020  sdot v0.4s, v1.16b, v2.4b[0]
//   0x6fa2e820  udot v0.4s, v1.16b, v2.4b[3]
//   0x0f82e020  sdot v0.2s, v1.8b,  v2.4b[0]
//   0x2fa2e820  udot v0.2s, v1.8b,  v2.4b[3]

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "hellodotprod"

namespace {

// Reference dot product: 4 byte products summed into a 32-bit lane, with
// 32-bit wraparound on overflow (matches ARM ARM C7.2.397 / C7.2.398).
int32_t ref_lane(const uint8_t* n4, const uint8_t* m4, bool is_signed,
                 int32_t acc_init) {
  int32_t acc = acc_init;
  for (int k = 0; k < 4; k++) {
    int32_t n_ext = is_signed ? static_cast<int32_t>(static_cast<int8_t>(n4[k]))
                              : static_cast<int32_t>(n4[k]);
    int32_t m_ext = is_signed ? static_cast<int32_t>(static_cast<int8_t>(m4[k]))
                              : static_cast<int32_t>(m4[k]);
    acc = static_cast<int32_t>(static_cast<uint32_t>(acc) +
                               static_cast<uint32_t>(n_ext * m_ext));
  }
  return acc;
}

// SDOT/UDOT vector .4S — 4 32-bit lanes accumulate dot of 4 bytes each.
// Pre-load Vd with non-zero `pre[]` to confirm read-modify-write.
template <bool is_signed>
bool probe_dot_4s_vec(std::string& report, char (&buf)[256]) {
  alignas(16) uint8_t n[16] = {
      0x01, 0x02, 0x7F, 0x80,   // lane 0: signed bytes mix positive/negative
      0xFF, 0xFE, 0x10, 0x20,   // lane 1
      0x05, 0x05, 0x05, 0x05,   // lane 2: all positive
      0x81, 0x82, 0x83, 0x84,   // lane 3: signed bytes all negative
  };
  alignas(16) uint8_t m[16] = {
      0x03, 0x04, 0x05, 0x06,
      0x07, 0x08, 0x09, 0x0A,
      0x0B, 0x0C, 0x0D, 0x0E,
      0x0F, 0x10, 0x11, 0x12,
  };
  alignas(16) int32_t pre[4] = {100, 200, 300, -50};
  alignas(16) int32_t out[4];
  std::memcpy(out, pre, 16);
  if constexpr (is_signed) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x4e829420  // sdot v0.4s, v1.16b, v2.16b\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6e829420  // udot v0.4s, v1.16b, v2.16b\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  }
  int32_t want[4];
  for (int i = 0; i < 4; i++) {
    want[i] = ref_lane(&n[4 * i], &m[4 * i], is_signed, pre[i]);
  }
  bool ok = out[0] == want[0] && out[1] == want[1] &&
            out[2] == want[2] && out[3] == want[3];
  snprintf(buf, sizeof(buf),
           "  %s .4S vec  out=[%d %d %d %d] want=[%d %d %d %d]: %s\n",
           is_signed ? "SDOT" : "UDOT",
           out[0], out[1], out[2], out[3],
           want[0], want[1], want[2], want[3],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// SDOT/UDOT vector .2S — 2 32-bit lanes; Q=0 zeroes upper 64 bits of Vd.
template <bool is_signed>
bool probe_dot_2s_vec(std::string& report, char (&buf)[256]) {
  alignas(16) uint8_t n[16] = {
      0x01, 0x02, 0x7F, 0x80,
      0xFF, 0xFE, 0x10, 0x20,
      // upper 64 bits unused by the .8B form; included for the LDR q1.
      0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
  };
  alignas(16) uint8_t m[16] = {
      0x03, 0x04, 0x05, 0x06,
      0x07, 0x08, 0x09, 0x0A,
      0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
  };
  // Pre-load 0xdeadbeef into the entire q0 to confirm the high 64 bits get
  // zeroed by Q=0 semantics (.2S form).
  alignas(16) uint32_t prev[4] = {0xdeadbeefu, 0xdeadbeefu,
                                   0xdeadbeefu, 0xdeadbeefu};
  alignas(16) int32_t pre[4] = {static_cast<int32_t>(0xdeadbeefu),
                                 static_cast<int32_t>(0xdeadbeefu),
                                 0, 0};
  // The visible-to-DOT pre-state for lanes [0,1] is whatever was in q0
  // before, i.e. the prev[] pattern.
  std::memcpy(&pre[0], &prev[0], 8);
  alignas(16) int32_t out[4];
  std::memcpy(out, prev, 16);
  if constexpr (is_signed) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x0e829420  // sdot v0.2s, v1.8b, v2.8b\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x2e829420  // udot v0.2s, v1.8b, v2.8b\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  }
  int32_t want[2];
  for (int i = 0; i < 2; i++) {
    want[i] = ref_lane(&n[4 * i], &m[4 * i], is_signed, pre[i]);
  }
  uint32_t out_hi[2];
  std::memcpy(out_hi, &out[2], 8);
  bool ok = out[0] == want[0] && out[1] == want[1] &&
            out_hi[0] == 0 && out_hi[1] == 0;
  snprintf(buf, sizeof(buf),
           "  %s .2S vec  out=[%d %d] hi=[%08x %08x] want=[%d %d]: %s\n",
           is_signed ? "SDOT" : "UDOT",
           out[0], out[1], out_hi[0], out_hi[1],
           want[0], want[1], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// SDOT/UDOT indexed .4S — Vm.4B[idx] broadcast across all 4 output lanes.
template <bool is_signed>
bool probe_dot_4s_idx(std::string& report, char (&buf)[256], int idx) {
  alignas(16) uint8_t n[16] = {
      0x01, 0x02, 0x7F, 0x80,
      0xFF, 0xFE, 0x10, 0x20,
      0x05, 0x05, 0x05, 0x05,
      0x81, 0x82, 0x83, 0x84,
  };
  // Each 4-byte index group in Vm carries a recognizable pattern so a
  // wrong broadcast (e.g. idx=0 instead of idx=3) gives a clearly wrong
  // numerical result.
  alignas(16) uint8_t m[16] = {
      0x03, 0x04, 0x05, 0x06,   // idx 0
      0x07, 0x08, 0x09, 0x0A,   // idx 1
      0x0B, 0x0C, 0x0D, 0x0E,   // idx 2
      0xF0, 0xF1, 0xF2, 0xF3,   // idx 3: signed bytes are negative
  };
  alignas(16) int32_t pre[4] = {100, 200, 300, -50};
  alignas(16) int32_t out[4];
  std::memcpy(out, pre, 16);
  if (is_signed && idx == 0) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x4f82e020  // sdot v0.4s, v1.16b, v2.4b[0]\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else if (!is_signed && idx == 3) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x6fa2e820  // udot v0.4s, v1.16b, v2.4b[3]\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {
    // No other index/sign combos used in this probe set; defensively skip.
    snprintf(buf, sizeof(buf),
             "  %s .4S idx[%d]  SKIP (no encoding probed)\n",
             is_signed ? "SDOT" : "UDOT", idx);
    report += buf;
    return false;
  }
  int32_t want[4];
  for (int i = 0; i < 4; i++) {
    want[i] = ref_lane(&n[4 * i], &m[4 * idx], is_signed, pre[i]);
  }
  bool ok = out[0] == want[0] && out[1] == want[1] &&
            out[2] == want[2] && out[3] == want[3];
  snprintf(buf, sizeof(buf),
           "  %s .4S idx[%d] out=[%d %d %d %d] want=[%d %d %d %d]: %s\n",
           is_signed ? "SDOT" : "UDOT", idx,
           out[0], out[1], out[2], out[3],
           want[0], want[1], want[2], want[3],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// SDOT/UDOT indexed .2S — Vm.4B[idx] broadcast across 2 output lanes;
// Q=0 zeroes the upper 64 bits.
template <bool is_signed>
bool probe_dot_2s_idx(std::string& report, char (&buf)[256], int idx) {
  alignas(16) uint8_t n[16] = {
      0x01, 0x02, 0x7F, 0x80,
      0xFF, 0xFE, 0x10, 0x20,
      0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99,
  };
  alignas(16) uint8_t m[16] = {
      0x03, 0x04, 0x05, 0x06,
      0x07, 0x08, 0x09, 0x0A,
      0x0B, 0x0C, 0x0D, 0x0E,
      0xF0, 0xF1, 0xF2, 0xF3,
  };
  alignas(16) uint32_t prev[4] = {0xdeadbeefu, 0xdeadbeefu,
                                   0xdeadbeefu, 0xdeadbeefu};
  alignas(16) int32_t pre[2];
  std::memcpy(&pre[0], &prev[0], 8);
  alignas(16) int32_t out[4];
  std::memcpy(out, prev, 16);
  if (is_signed && idx == 0) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x0f82e020  // sdot v0.2s, v1.8b, v2.4b[0]\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else if (!is_signed && idx == 3) {
    __asm__ __volatile__(
        "ldr q1, [%0]\n"
        "ldr q2, [%1]\n"
        "ldr q0, [%2]\n"
        ".inst 0x2fa2e820  // udot v0.2s, v1.8b, v2.4b[3]\n"
        "str q0, [%2]\n"
        :
        : "r"(n), "r"(m), "r"(out)
        : "v0", "v1", "v2", "memory");
  } else {
    snprintf(buf, sizeof(buf),
             "  %s .2S idx[%d]  SKIP (no encoding probed)\n",
             is_signed ? "SDOT" : "UDOT", idx);
    report += buf;
    return false;
  }
  int32_t want[2];
  for (int i = 0; i < 2; i++) {
    want[i] = ref_lane(&n[4 * i], &m[4 * idx], is_signed, pre[i]);
  }
  uint32_t out_hi[2];
  std::memcpy(out_hi, &out[2], 8);
  bool ok = out[0] == want[0] && out[1] == want[1] &&
            out_hi[0] == 0 && out_hi[1] == 0;
  snprintf(buf, sizeof(buf),
           "  %s .2S idx[%d] out=[%d %d] hi=[%08x %08x] want=[%d %d]: %s\n",
           is_signed ? "SDOT" : "UDOT", idx,
           out[0], out[1], out_hi[0], out_hi[1],
           want[0], want[1], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// region digitalis - I8MM (FEAT_I8MM): USDOT (mixed-sign dot product) and the
// integer matrix-multiply-accumulate SMMLA/UMMLA/USMMLA. Emitted via raw .inst
// so the probe builds without an +i8mm toolchain. Vn byte0=0x80 / byte8=0xFF
// distinguish signed vs unsigned operand handling, so SMMLA/UMMLA/USMMLA must
// produce distinct results.
bool probe_i8mm(std::string& report, char (&buf)[256]) {
  alignas(16) uint8_t n[16] = {0x80, 2, 3, 4, 5, 6, 7, 8,
                               0xFF, 2, 2, 2, 1, 1, 1, 1};
  alignas(16) uint8_t m[16] = {0x01, 0x01, 0x01, 0x01, 0xFF, 0xFF, 0xFF, 0xFF,
                               0x02, 0x02, 0x02, 0x02, 0xFE, 0xFE, 0xFE, 0xFE};
  alignas(16) int32_t out[4];
#define RUN_I8MM(enc)                                          \
  std::memset(out, 0, sizeof(out));                            \
  __asm__ __volatile__("ldr q1, [%0]\n"                        \
                       "ldr q2, [%1]\n"                        \
                       "ldr q0, [%2]\n"                        \
                       ".inst " #enc "\n"                      \
                       "str q0, [%2]\n"                        \
                       :                                       \
                       : "r"(n), "r"(m), "r"(out)              \
                       : "v0", "v1", "v2", "memory")
  bool ok = true;
  RUN_I8MM(0x4E829C20);  // usdot v0.4s, v1.16b, v2.16b
  ok &= (static_cast<uint32_t>(out[0]) == 0x00000089u &&
         static_cast<uint32_t>(out[1]) == 0xffffffe6u &&
         static_cast<uint32_t>(out[2]) == 0x0000020au &&
         static_cast<uint32_t>(out[3]) == 0xfffffff8u);
  RUN_I8MM(0x4E82A420);  // smmla v0.4s, v1.16b, v2.16b
  ok &= (static_cast<uint32_t>(out[0]) == 0xffffff6fu &&
         static_cast<uint32_t>(out[1]) == 0xfffffedeu &&
         out[2] == 0x00000001 && out[3] == 0x00000002);
  RUN_I8MM(0x6E82A420);  // ummla v0.4s, v1.16b, v2.16b
  ok &= (out[0] == 0x00001a6f && out[1] == 0x00001ade &&
         out[2] == 0x00000501 && out[3] == 0x00000602);
  RUN_I8MM(0x4E82AC20);  // usmmla v0.4s, v1.16b, v2.16b
  ok &= (out[0] == 0x0000006f && out[1] == 0x000000de &&
         out[2] == 0x00000101 && out[3] == 0x00000202);
#undef RUN_I8MM
  snprintf(buf, sizeof(buf), "  I8MM USDOT/SMMLA/UMMLA/USMMLA: %s\n",
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}
// endregion

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellodotprod_MainActivity_probeDotProd(JNIEnv* env,
                                                        jobject /*this*/) {
  std::string report = "Armv8.4-DotProd instruction probe:\n";
  char buf[256];
  int total = 0, passed = 0;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Armv8.4-DotProd probe:");

  auto run = [&](bool ok) {
    total++;
    if (ok) passed++;
  };

  run(probe_dot_4s_vec<true>(report, buf));    // SDOT .4S vec
  run(probe_dot_4s_vec<false>(report, buf));   // UDOT .4S vec
  run(probe_dot_2s_vec<true>(report, buf));    // SDOT .2S vec
  run(probe_dot_2s_vec<false>(report, buf));   // UDOT .2S vec
  run(probe_dot_4s_idx<true>(report, buf, 0)); // SDOT .4S idx[0]
  run(probe_dot_4s_idx<false>(report, buf, 3));// UDOT .4S idx[3]
  run(probe_dot_2s_idx<true>(report, buf, 0)); // SDOT .2S idx[0]
  run(probe_dot_2s_idx<false>(report, buf, 3));// UDOT .2S idx[3]
  run(probe_i8mm(report, buf));                // I8MM USDOT/SMMLA/UMMLA/USMMLA

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Summary: %d/%d OK",
                      passed, total);
  return env->NewStringUTF(report.c_str());
}
