// Exercises the ARMv8 cryptographic-extension SHA family across all three
// digest sizes — SHA-1, SHA-256, and SHA-512 — against portable C
// references derived from FIPS-180-4.
//
// Per-family ops:
//   SHA-1  : SHA1H, SHA1C, SHA1P, SHA1M
//            (ARM ARM C7.2.71/72/73/74)
//   SHA-256: SHA256H, SHA256H2, SHA256SU0, SHA256SU1
//            (ARM ARM C7.2.77/78/79/80)
//   SHA-512: SHA512H, SHA512H2, SHA512SU0, SHA512SU1
//            (ARM ARM C7.2.85/86/87/88; FEAT_SHA512, ARMv8.2+)
//
// Each intrinsic is invoked, the result compared to a portable C reference,
// and any divergence surfaces as a FAIL line on screen. If the underlying
// instruction is not yet implemented in the Digitalis translator, the
// process SIGILLs — which is the correct way to find translator gaps.

#include <arm_neon.h>
#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellosha"

namespace {

inline uint32_t rol32(uint32_t x, unsigned n) {
  return (x << n) | (x >> (32u - n));
}
inline uint32_t ror32(uint32_t x, unsigned n) {
  return (x >> n) | (x << (32u - n));
}
inline uint64_t ror64(uint64_t x, unsigned n) {
  return (x >> n) | (x << (64u - n));
}

//===========================================================================
// SHA-1 round helpers and reference
//===========================================================================
inline uint32_t sha1_f_ch(uint32_t b, uint32_t c, uint32_t d) {
  return (b & c) | (~b & d);
}
inline uint32_t sha1_f_par(uint32_t b, uint32_t c, uint32_t d) {
  return b ^ c ^ d;
}
inline uint32_t sha1_f_maj(uint32_t b, uint32_t c, uint32_t d) {
  return (b & c) | (b & d) | (c & d);
}

template <uint32_t F(uint32_t, uint32_t, uint32_t)>
void sha1_round4_ref(const uint32_t abcd_in[4], uint32_t e_in,
                     const uint32_t wk[4], uint32_t abcd_out[4]) {
  uint32_t a = abcd_in[0], b = abcd_in[1], c = abcd_in[2], d = abcd_in[3];
  uint32_t e = e_in;
  for (int j = 0; j < 4; ++j) {
    uint32_t t = rol32(a, 5) + F(b, c, d) + e + wk[j];
    e = d; d = c; c = rol32(b, 30); b = a; a = t;
  }
  abcd_out[0] = a; abcd_out[1] = b; abcd_out[2] = c; abcd_out[3] = d;
}

constexpr uint32_t kSha1InitAbcd[4] = {
    0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
constexpr uint32_t kSha1InitE = 0xc3d2e1f0u;

//===========================================================================
// SHA-256 round helpers and reference (FIPS-180-4 sec 4.1.2)
//===========================================================================
inline uint32_t big_sigma0_32(uint32_t x) {
  return ror32(x, 2) ^ ror32(x, 13) ^ ror32(x, 22);
}
inline uint32_t big_sigma1_32(uint32_t x) {
  return ror32(x, 6) ^ ror32(x, 11) ^ ror32(x, 25);
}
inline uint32_t little_sigma0_32(uint32_t x) {
  return ror32(x, 7) ^ ror32(x, 18) ^ (x >> 3);
}
inline uint32_t little_sigma1_32(uint32_t x) {
  return ror32(x, 17) ^ ror32(x, 19) ^ (x >> 10);
}
inline uint32_t ch32(uint32_t e, uint32_t f, uint32_t g) {
  return (e & f) ^ (~e & g);
}
inline uint32_t maj32(uint32_t a, uint32_t b, uint32_t c) {
  return (a & b) ^ (a & c) ^ (b & c);
}

void sha256_round4_ref(const uint32_t abcd_in[4], const uint32_t efgh_in[4],
                       const uint32_t wk[4],
                       uint32_t abcd_out[4], uint32_t efgh_out[4]) {
  uint32_t a = abcd_in[0], b = abcd_in[1], c = abcd_in[2], d = abcd_in[3];
  uint32_t e = efgh_in[0], f = efgh_in[1], g = efgh_in[2], h = efgh_in[3];
  for (int j = 0; j < 4; ++j) {
    uint32_t t1 = h + big_sigma1_32(e) + ch32(e, f, g) + wk[j];
    uint32_t t2 = big_sigma0_32(a) + maj32(a, b, c);
    h = g; g = f; f = e; e = d + t1;
    d = c; c = b; b = a; a = t1 + t2;
  }
  abcd_out[0] = a; abcd_out[1] = b; abcd_out[2] = c; abcd_out[3] = d;
  efgh_out[0] = e; efgh_out[1] = f; efgh_out[2] = g; efgh_out[3] = h;
}

constexpr uint32_t kSha256InitAbcd[4] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au};
constexpr uint32_t kSha256InitEfgh[4] = {
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

//===========================================================================
// SHA-512 round helpers and reference (FIPS-180-4 sec 4.1.3)
//===========================================================================
inline uint64_t big_sigma0_64(uint64_t x) {
  return ror64(x, 28) ^ ror64(x, 34) ^ ror64(x, 39);
}
inline uint64_t big_sigma1_64(uint64_t x) {
  return ror64(x, 14) ^ ror64(x, 18) ^ ror64(x, 41);
}
inline uint64_t little_sigma0_64(uint64_t x) {
  return ror64(x, 1) ^ ror64(x, 8) ^ (x >> 7);
}
inline uint64_t little_sigma1_64(uint64_t x) {
  return ror64(x, 19) ^ ror64(x, 61) ^ (x >> 6);
}

// SHA512H Vd.2D, Vn.2D, Vm.2D (ARM ARM C7.2.85):
//   Tmp64 = (Vm[0] & Vn[0]) ^ (~Vm[0] & Vn[1])  // Ch using Vm[0] as e
//           + Sigma1(Vm[0]) + Vd[1]
//   Vd[1] = Vd[0] + Tmp64
//   Vd[0] = Tmp64
void sha512h_ref(const uint64_t d_in[2], const uint64_t n_in[2],
                 const uint64_t m_in[2], uint64_t d_out[2]) {
  uint64_t e = m_in[0];
  uint64_t f = n_in[0];
  uint64_t g = n_in[1];
  uint64_t tmp = ((e & f) ^ (~e & g)) + big_sigma1_64(e) + d_in[1];
  d_out[1] = d_in[0] + tmp;
  d_out[0] = tmp;
}

// SHA512H2 Vd.2D, Vn.2D, Vm.2D (ARM ARM C7.2.86):
//   new_d[1] = d[1] + Sigma0(Vn[0]) + Maj(Vn[0], Vm[0], Vm[1])
//   new_d[0] = d[0] + Sigma0(new_d[1]) + Maj(new_d[1], Vn[0], Vm[0])
void sha512h2_ref(const uint64_t d_in[2], const uint64_t n_in[2],
                  const uint64_t m_in[2], uint64_t d_out[2]) {
  auto maj = [](uint64_t a, uint64_t b, uint64_t c) -> uint64_t {
    return (a & b) ^ (a & c) ^ (b & c);
  };
  uint64_t new_d1 = d_in[1] + big_sigma0_64(n_in[0]) +
                    maj(n_in[0], m_in[0], m_in[1]);
  uint64_t new_d0 = d_in[0] + big_sigma0_64(new_d1) +
                    maj(new_d1, n_in[0], m_in[0]);
  d_out[0] = new_d0;
  d_out[1] = new_d1;
}

// SHA512SU0 Vd.2D, Vn.2D (ARM ARM C7.2.87):
//   Vd[0] += sigma0(Vd[1])
//   Vd[1] += sigma0(Vn[0])
void sha512su0_ref(const uint64_t d_in[2], const uint64_t n_in[2],
                   uint64_t d_out[2]) {
  d_out[0] = d_in[0] + little_sigma0_64(d_in[1]);
  d_out[1] = d_in[1] + little_sigma0_64(n_in[0]);
}

// SHA512SU1 Vd.2D, Vn.2D, Vm.2D (ARM ARM C7.2.88):
//   Vd[0] += sigma1(Vn[0]) + Vm[0]
//   Vd[1] += sigma1(Vn[1]) + Vm[1]
void sha512su1_ref(const uint64_t d_in[2], const uint64_t n_in[2],
                   const uint64_t m_in[2], uint64_t d_out[2]) {
  d_out[0] = d_in[0] + little_sigma1_64(n_in[0]) + m_in[0];
  d_out[1] = d_in[1] + little_sigma1_64(n_in[1]) + m_in[1];
}

constexpr uint64_t kSha512InitAb[2] = {0x6a09e667f3bcc908ULL,
                                       0xbb67ae8584caa73bULL};
constexpr uint64_t kSha512InitCd[2] = {0x3c6ef372fe94f82bULL,
                                       0xa54ff53a5f1d36f1ULL};

//===========================================================================
// Per-instruction probes
//===========================================================================
bool probe_sha1c(std::string* log) {
  alignas(16) uint32_t wk[4] = {0x5a827999u, 0x5a82799au,
                                0x5a82799bu, 0x5a82799cu};
  uint32_t want[4];
  sha1_round4_ref<sha1_f_ch>(kSha1InitAbcd, kSha1InitE, wk, want);

  alignas(16) uint32_t got[4];
  vst1q_u32(got, vsha1cq_u32(vld1q_u32(kSha1InitAbcd), kSha1InitE,
                             vld1q_u32(wk)));
  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA1C     %s\n", ok ? "OK" : "FAIL");
  *log += buf;
  return ok;
}

bool probe_sha1p(std::string* log) {
  alignas(16) uint32_t wk[4] = {0x6ed9eba1u, 0x6ed9eba2u,
                                0x6ed9eba3u, 0x6ed9eba4u};
  uint32_t want[4];
  sha1_round4_ref<sha1_f_par>(kSha1InitAbcd, kSha1InitE, wk, want);

  alignas(16) uint32_t got[4];
  vst1q_u32(got, vsha1pq_u32(vld1q_u32(kSha1InitAbcd), kSha1InitE,
                             vld1q_u32(wk)));
  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA1P     %s\n", ok ? "OK" : "FAIL");
  *log += buf;
  return ok;
}

bool probe_sha1m(std::string* log) {
  alignas(16) uint32_t wk[4] = {0x8f1bbcdcu, 0x8f1bbcddu,
                                0x8f1bbcdeu, 0x8f1bbcdfu};
  uint32_t want[4];
  sha1_round4_ref<sha1_f_maj>(kSha1InitAbcd, kSha1InitE, wk, want);

  alignas(16) uint32_t got[4];
  vst1q_u32(got, vsha1mq_u32(vld1q_u32(kSha1InitAbcd), kSha1InitE,
                             vld1q_u32(wk)));
  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA1M     %s\n", ok ? "OK" : "FAIL");
  *log += buf;
  return ok;
}

bool probe_sha1h(std::string* log) {
  const uint32_t inputs[] = {0x12345678u, 0x00000003u, 0xfffffffcu,
                             0xc3d2e1f0u, 0u};
  bool all_ok = true;
  for (uint32_t x : inputs) {
    if (vsha1h_u32(x) != ((x >> 2) | (x << 30))) {
      all_ok = false;
      break;
    }
  }
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA1H     %s\n", all_ok ? "OK" : "FAIL");
  *log += buf;
  return all_ok;
}

bool probe_sha256h_h2(std::string* log) {
  alignas(16) uint32_t wk[4] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u};
  uint32_t want_abcd[4], want_efgh[4];
  sha256_round4_ref(kSha256InitAbcd, kSha256InitEfgh, wk,
                    want_abcd, want_efgh);

  uint32x4_t v_abcd = vld1q_u32(kSha256InitAbcd);
  uint32x4_t v_efgh = vld1q_u32(kSha256InitEfgh);
  uint32x4_t v_wk = vld1q_u32(wk);
  alignas(16) uint32_t got_abcd[4], got_efgh[4];
  vst1q_u32(got_abcd, vsha256hq_u32(v_abcd, v_efgh, v_wk));
  vst1q_u32(got_efgh, vsha256h2q_u32(v_efgh, v_abcd, v_wk));

  bool ok_abcd = got_abcd[0] == want_abcd[0] && got_abcd[1] == want_abcd[1] &&
                 got_abcd[2] == want_abcd[2] && got_abcd[3] == want_abcd[3];
  bool ok_efgh = got_efgh[0] == want_efgh[0] && got_efgh[1] == want_efgh[1] &&
                 got_efgh[2] == want_efgh[2] && got_efgh[3] == want_efgh[3];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA256H   %s\n", ok_abcd ? "OK" : "FAIL");
  *log += buf;
  snprintf(buf, sizeof(buf), "  SHA256H2  %s\n", ok_efgh ? "OK" : "FAIL");
  *log += buf;
  return ok_abcd && ok_efgh;
}

bool probe_sha256su0(std::string* log) {
  alignas(16) uint32_t d_in[4] = {kSha256InitAbcd[0], kSha256InitAbcd[1],
                                  kSha256InitAbcd[2], kSha256InitAbcd[3]};
  alignas(16) uint32_t n_in[4] = {kSha256InitEfgh[0], kSha256InitEfgh[1],
                                  kSha256InitEfgh[2], kSha256InitEfgh[3]};
  uint32_t want[4] = {
      d_in[0] + little_sigma0_32(d_in[1]),
      d_in[1] + little_sigma0_32(d_in[2]),
      d_in[2] + little_sigma0_32(d_in[3]),
      d_in[3] + little_sigma0_32(n_in[0])};
  alignas(16) uint32_t got[4];
  vst1q_u32(got, vsha256su0q_u32(vld1q_u32(d_in), vld1q_u32(n_in)));
  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA256SU0 %s\n", ok ? "OK" : "FAIL");
  *log += buf;
  return ok;
}

bool probe_sha256su1(std::string* log) {
  alignas(16) uint32_t d_in[4] = {kSha256InitAbcd[0], kSha256InitAbcd[1],
                                  kSha256InitAbcd[2], kSha256InitAbcd[3]};
  alignas(16) uint32_t n_in[4] = {0x428a2f98u, 0x71374491u, 0xb5c0fbcfu,
                                  0xe9b5dba5u};
  alignas(16) uint32_t m_in[4] = {0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
                                  0xab1c5ed5u};
  uint32_t nd0 = d_in[0] + little_sigma1_32(m_in[2]) + n_in[1];
  uint32_t nd1 = d_in[1] + little_sigma1_32(m_in[3]) + n_in[2];
  uint32_t nd2 = d_in[2] + little_sigma1_32(nd0) + n_in[3];
  uint32_t nd3 = d_in[3] + little_sigma1_32(nd1) + m_in[0];
  uint32_t want[4] = {nd0, nd1, nd2, nd3};
  alignas(16) uint32_t got[4];
  vst1q_u32(got, vsha256su1q_u32(vld1q_u32(d_in),
                                 vld1q_u32(n_in),
                                 vld1q_u32(m_in)));
  bool ok = got[0] == want[0] && got[1] == want[1] &&
            got[2] == want[2] && got[3] == want[3];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA256SU1 %s\n", ok ? "OK" : "FAIL");
  *log += buf;
  return ok;
}

bool probe_sha512h_h2(std::string* log) {
  alignas(16) uint64_t d[2] = {kSha512InitAb[0], kSha512InitAb[1]};
  alignas(16) uint64_t n[2] = {kSha512InitCd[0], kSha512InitCd[1]};
  alignas(16) uint64_t m[2] = {0x428a2f98d728ae22ULL,
                               0x7137449123ef65cdULL};

  uint64_t want_h[2], want_h2[2];
  sha512h_ref(d, n, m, want_h);
  sha512h2_ref(d, n, m, want_h2);

  uint64x2_t vd = vld1q_u64(d);
  uint64x2_t vn = vld1q_u64(n);
  uint64x2_t vm = vld1q_u64(m);
  alignas(16) uint64_t got_h[2], got_h2[2];
  vst1q_u64(got_h, vsha512hq_u64(vd, vn, vm));
  vst1q_u64(got_h2, vsha512h2q_u64(vd, vn, vm));

  bool ok_h = got_h[0] == want_h[0] && got_h[1] == want_h[1];
  bool ok_h2 = got_h2[0] == want_h2[0] && got_h2[1] == want_h2[1];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA512H   %s\n", ok_h ? "OK" : "FAIL");
  *log += buf;
  snprintf(buf, sizeof(buf), "  SHA512H2  %s\n", ok_h2 ? "OK" : "FAIL");
  *log += buf;
  return ok_h && ok_h2;
}

bool probe_sha512su0(std::string* log) {
  alignas(16) uint64_t d[2] = {kSha512InitAb[0], kSha512InitAb[1]};
  alignas(16) uint64_t n[2] = {kSha512InitCd[0], kSha512InitCd[1]};
  uint64_t want[2];
  sha512su0_ref(d, n, want);
  alignas(16) uint64_t got[2];
  vst1q_u64(got, vsha512su0q_u64(vld1q_u64(d), vld1q_u64(n)));
  bool ok = got[0] == want[0] && got[1] == want[1];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA512SU0 %s\n", ok ? "OK" : "FAIL");
  *log += buf;
  return ok;
}

bool probe_sha512su1(std::string* log) {
  alignas(16) uint64_t d[2] = {kSha512InitAb[0], kSha512InitAb[1]};
  alignas(16) uint64_t n[2] = {kSha512InitCd[0], kSha512InitCd[1]};
  alignas(16) uint64_t m[2] = {0x428a2f98d728ae22ULL,
                               0x7137449123ef65cdULL};
  uint64_t want[2];
  sha512su1_ref(d, n, m, want);
  alignas(16) uint64_t got[2];
  vst1q_u64(got, vsha512su1q_u64(vld1q_u64(d),
                                 vld1q_u64(n),
                                 vld1q_u64(m)));
  bool ok = got[0] == want[0] && got[1] == want[1];
  char buf[80];
  snprintf(buf, sizeof(buf), "  SHA512SU1 %s\n", ok ? "OK" : "FAIL");
  *log += buf;
  return ok;
}

// SHA3 (FEAT_SHA3): EOR3 / BCAX / RAX1 / XAR.
bool probe_sha3(std::string* log) {
  alignas(16) uint64_t a[2] = {0x1111111111111111ULL, 0xAAAAAAAAAAAAAAAAULL};
  alignas(16) uint64_t b[2] = {0x2222222222222222ULL, 0x5555555555555555ULL};
  alignas(16) uint64_t c[2] = {0x4444444444444444ULL, 0x0F0F0F0F0F0F0F0FULL};
  uint64x2_t va = vld1q_u64(a), vb = vld1q_u64(b), vc = vld1q_u64(c);
  alignas(16) uint64_t got[2];
  auto rol1 = [](uint64_t x) { return (x << 1) | (x >> 63); };
  auto ror = [](uint64_t x, int n) { return (x >> n) | (x << (64 - n)); };

  vst1q_u64(got, veor3q_u64(va, vb, vc));
  bool eor3_ok = got[0] == (a[0] ^ b[0] ^ c[0]) && got[1] == (a[1] ^ b[1] ^ c[1]);

  vst1q_u64(got, vbcaxq_u64(va, vb, vc));
  bool bcax_ok = got[0] == (a[0] ^ (b[0] & ~c[0])) &&
                 got[1] == (a[1] ^ (b[1] & ~c[1]));

  vst1q_u64(got, vrax1q_u64(va, vb));
  bool rax1_ok = got[0] == (a[0] ^ rol1(b[0])) && got[1] == (a[1] ^ rol1(b[1]));

  vst1q_u64(got, vxarq_u64(va, vb, 4));
  bool xar_ok = got[0] == ror(a[0] ^ b[0], 4) && got[1] == ror(a[1] ^ b[1], 4);

  bool all_ok = eor3_ok && bcax_ok && rax1_ok && xar_ok;
  char buf[96];
  snprintf(buf, sizeof(buf), "  SHA3 EOR3=%s BCAX=%s RAX1=%s XAR=%s\n",
           eor3_ok ? "OK" : "FAIL", bcax_ok ? "OK" : "FAIL",
           rax1_ok ? "OK" : "FAIL", xar_ok ? "OK" : "FAIL");
  *log += buf;
  return all_ok;
}

// SM4 (FEAT_SM4): SM4E (4 encryption rounds) and SM4EKEY (4 key-expansion
// steps). Validated with the single-instruction steps of the GB/T 32907
// standard test vector (whose full flow yields ciphertext 681edf34...).
bool probe_sm4(std::string* log) {
  // SM4EKEY: K^FK seed + CK0..3 -> round keys 0..3.
  alignas(16) uint32_t ek_n[4] = {0xa292ffa1u, 0xdf01febfu, 0x99a12b0fu, 0xc42410ccu};
  alignas(16) uint32_t ek_m[4] = {0x00070e15u, 0x1c232a31u, 0x383f464du, 0x545b6269u};
  alignas(16) uint32_t want_rk[4] = {0xf12186f9u, 0x41662b61u, 0x5a6ab19au, 0x7ba92077u};
  alignas(16) uint32_t got[4];
  vst1q_u32(got, vsm4ekeyq_u32(vld1q_u32(ek_n), vld1q_u32(ek_m)));
  bool ekey_ok = got[0] == want_rk[0] && got[1] == want_rk[1] &&
                 got[2] == want_rk[2] && got[3] == want_rk[3];

  // SM4E: plaintext state + round keys 0..3 -> state after 4 rounds.
  alignas(16) uint32_t e_d[4] = {0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u};
  alignas(16) uint32_t want_e[4] = {0x27fad345u, 0xa18b4cb2u, 0x11c1e22au, 0xcc13e2eeu};
  vst1q_u32(got, vsm4eq_u32(vld1q_u32(e_d), vld1q_u32(want_rk)));
  bool e_ok = got[0] == want_e[0] && got[1] == want_e[1] &&
              got[2] == want_e[2] && got[3] == want_e[3];

  char buf[96];
  snprintf(buf, sizeof(buf), "  SM4 SM4EKEY=%s SM4E=%s\n",
           ekey_ok ? "OK" : "FAIL", e_ok ? "OK" : "FAIL");
  *log += buf;
  return ekey_ok && e_ok;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellosha_MainActivity_probeShaCrypto(JNIEnv* env,
                                                      jobject /*this*/) {
  std::string report = "ARMv8 SHA crypto extension probe:\n";

  bool all_ok = true;
  all_ok &= probe_sha1h(&report);
  all_ok &= probe_sha1c(&report);
  all_ok &= probe_sha1p(&report);
  all_ok &= probe_sha1m(&report);
  all_ok &= probe_sha256h_h2(&report);
  all_ok &= probe_sha256su0(&report);
  all_ok &= probe_sha256su1(&report);
  all_ok &= probe_sha512h_h2(&report);
  all_ok &= probe_sha512su0(&report);
  all_ok &= probe_sha512su1(&report);
  all_ok &= probe_sha3(&report);
  all_ok &= probe_sm4(&report);

  report += all_ok ? "All SHA crypto ops OK.\n"
                   : "One or more SHA crypto ops FAILED.\n";

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
