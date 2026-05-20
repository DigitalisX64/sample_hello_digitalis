// Exercises the SHA-1 cryptographic extension instructions that the
// Digitalis interpreter implements:
//
//   SHA1H  — single-input word rotate by 30 (ROR by 2).
//   SHA1C  — four-round mix with Ch choice function.
//   SHA1P  — four-round mix with parity choice function.
//   SHA1M  — four-round mix with Maj choice function.
//
// Each one is invoked via its NEON intrinsic and compared against a
// portable C reference, so any silent decoder mis-routing or wrong
// inner-function selection in the interpreter surfaces as a FAIL.
//
// Spec: ARM ARM C7.2.71/72/73 (SHA1C/P/M), C7.2.74 (SHA1H).

#include <arm_neon.h>
#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellosha1crypto"

namespace {

inline uint32_t rol(uint32_t x, unsigned n) {
  return (x << n) | (x >> (32u - n));
}

// SHA-1 round choice functions (one per instruction).
inline uint32_t f_ch(uint32_t b, uint32_t c, uint32_t d) {
  return (b & c) | (~b & d);
}
inline uint32_t f_par(uint32_t b, uint32_t c, uint32_t d) {
  return b ^ c ^ d;
}
inline uint32_t f_maj(uint32_t b, uint32_t c, uint32_t d) {
  return (b & c) | (b & d) | (c & d);
}

// Portable reference for one 4-round block of SHA1{C,P,M}q_u32:
//
//   for j in 0..3 {
//       t = ROL(A, 5) + f(B, C, D) + E + wk[j];
//       E = D; D = C; C = ROL(B, 30); B = A; A = t;
//   }
//   returns the updated {A, B, C, D} -- E is consumed.
//
// The intrinsic's e argument is the *initial* scalar E.
template <uint32_t F(uint32_t, uint32_t, uint32_t)>
void sha1_round4_ref(const uint32_t abcd_in[4], uint32_t e_in,
                     const uint32_t wk[4], uint32_t abcd_out[4]) {
  uint32_t a = abcd_in[0], b = abcd_in[1], c = abcd_in[2], d = abcd_in[3];
  uint32_t e = e_in;
  for (int j = 0; j < 4; ++j) {
    uint32_t t = rol(a, 5) + F(b, c, d) + e + wk[j];
    e = d;
    d = c;
    c = rol(b, 30);
    b = a;
    a = t;
  }
  abcd_out[0] = a;
  abcd_out[1] = b;
  abcd_out[2] = c;
  abcd_out[3] = d;
}

// Standard SHA-1 initial hash values + first round W+K constant — gives
// the probe a "real" input domain instead of arbitrary bit patterns.
constexpr uint32_t kSha1InitAbcd[4] = {
    0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
constexpr uint32_t kSha1InitE = 0xc3d2e1f0u;

bool probe_sha1c(std::string* log) {
  alignas(16) uint32_t wk[4] = {0x5a827999u, 0x5a82799au,
                                0x5a82799bu, 0x5a82799cu};
  uint32_t want[4];
  sha1_round4_ref<f_ch>(kSha1InitAbcd, kSha1InitE, wk, want);

  uint32x4_t got_v = vsha1cq_u32(vld1q_u32(kSha1InitAbcd), kSha1InitE,
                                 vld1q_u32(wk));
  alignas(16) uint32_t got[4];
  vst1q_u32(got, got_v);

  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[160];
  snprintf(buf, sizeof(buf), "  SHA1C  %s  got=%08x,%08x,%08x,%08x\n",
           ok ? "OK" : "FAIL", got[0], got[1], got[2], got[3]);
  *log += buf;
  return ok;
}

bool probe_sha1p(std::string* log) {
  // Round-20 constant range — SHA1P is used in rounds 20..39.
  alignas(16) uint32_t wk[4] = {0x6ed9eba1u, 0x6ed9eba2u,
                                0x6ed9eba3u, 0x6ed9eba4u};
  uint32_t want[4];
  sha1_round4_ref<f_par>(kSha1InitAbcd, kSha1InitE, wk, want);

  uint32x4_t got_v = vsha1pq_u32(vld1q_u32(kSha1InitAbcd), kSha1InitE,
                                 vld1q_u32(wk));
  alignas(16) uint32_t got[4];
  vst1q_u32(got, got_v);

  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[160];
  snprintf(buf, sizeof(buf), "  SHA1P  %s  got=%08x,%08x,%08x,%08x\n",
           ok ? "OK" : "FAIL", got[0], got[1], got[2], got[3]);
  *log += buf;
  return ok;
}

bool probe_sha1m(std::string* log) {
  // Round-40 constant range — SHA1M is used in rounds 40..59.
  alignas(16) uint32_t wk[4] = {0x8f1bbcdcu, 0x8f1bbcddu,
                                0x8f1bbcdeu, 0x8f1bbcdfu};
  uint32_t want[4];
  sha1_round4_ref<f_maj>(kSha1InitAbcd, kSha1InitE, wk, want);

  uint32x4_t got_v = vsha1mq_u32(vld1q_u32(kSha1InitAbcd), kSha1InitE,
                                 vld1q_u32(wk));
  alignas(16) uint32_t got[4];
  vst1q_u32(got, got_v);

  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[160];
  snprintf(buf, sizeof(buf), "  SHA1M  %s  got=%08x,%08x,%08x,%08x\n",
           ok ? "OK" : "FAIL", got[0], got[1], got[2], got[3]);
  *log += buf;
  return ok;
}

bool probe_sha1h(std::string* log) {
  // SHA1H performs ROR(x, 2) on the low word. Verify across a few
  // patterns including the carry-from-bottom edges.
  const uint32_t inputs[] = {0x12345678u, 0x00000003u, 0xfffffffcu,
                             0xc3d2e1f0u, 0u};
  bool all_ok = true;
  std::string detail;
  for (uint32_t x : inputs) {
    uint32_t want = (x >> 2) | (x << 30);
    uint32_t got = vsha1h_u32(x);
    bool ok = got == want;
    all_ok = all_ok && ok;
    char buf[80];
    snprintf(buf, sizeof(buf), "%08x->%08x%s ", x, got, ok ? "" : "(!)");
    detail += buf;
  }
  char buf[256];
  snprintf(buf, sizeof(buf), "  SHA1H  %s  %s\n",
           all_ok ? "OK" : "FAIL", detail.c_str());
  *log += buf;
  return all_ok;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellosha1crypto_MainActivity_probeSha1Crypto(JNIEnv* env,
                                                              jobject /*this*/) {
  std::string report = "SHA-1 crypto extension probe:\n";

  bool h = probe_sha1h(&report);
  bool c = probe_sha1c(&report);
  bool p = probe_sha1p(&report);
  bool m = probe_sha1m(&report);

  report += (h && c && p && m) ? "All SHA-1 crypto ops OK.\n"
                               : "One or more SHA-1 crypto ops FAILED.\n";

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
