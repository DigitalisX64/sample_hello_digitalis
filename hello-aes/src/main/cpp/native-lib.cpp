// Integration-level probe for the ARMv8 AES crypto-extension instructions:
// AESE, AESD, AESMC, AESIMC (via the <arm_neon.h> crypto intrinsics).
//
// These are the block-cipher primitives BoringSSL uses for AES-GCM, so a
// mis-translation silently breaks every TLS handshake (observed as
// net::ERR_SSL_PROTOCOL_ERROR in Cronet, e.g. com.netease.cloudmusic). The
// probe runs the FIPS-197 Appendix C.1 AES-128 known-answer test end to end
// (encrypt then decrypt) so a wrong S-box / ShiftRows / MixColumns shows up as
// a ciphertext mismatch rather than a crash.
//
//   key        = 000102030405060708090a0b0c0d0e0f
//   plaintext  = 00112233445566778899aabbccddeeff
//   ciphertext = 69c4e0d86a7b0430d8cdb78070b4c55a   (FIPS-197 C.1)

#include <android/log.h>
#include <arm_neon.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "helloaes"

namespace {

// AES S-box (FIPS-197) for the plain-C key schedule. The round instructions
// (AESE/AESMC) under test carry their own S-box in hardware; this table only
// drives key expansion so the test isolates the instructions.
const uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};

// Standard AES-128 key expansion -> 11 round keys (16 bytes each).
void ExpandKey128(const uint8_t key[16], uint8_t rk[11][16]) {
  static const uint8_t rcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};
  uint8_t w[44][4];
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++) w[i][j] = key[i * 4 + j];
  for (int i = 4; i < 44; i++) {
    uint8_t t[4] = {w[i - 1][0], w[i - 1][1], w[i - 1][2], w[i - 1][3]};
    if (i % 4 == 0) {
      uint8_t tmp = t[0];
      t[0] = kSbox[t[1]] ^ rcon[i / 4 - 1];
      t[1] = kSbox[t[2]];
      t[2] = kSbox[t[3]];
      t[3] = kSbox[tmp];
    }
    for (int j = 0; j < 4; j++) w[i][j] = w[i - 4][j] ^ t[j];
  }
  for (int r = 0; r < 11; r++)
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++) rk[r][i * 4 + j] = w[r * 4 + i][j];
}

// AES-128 block encrypt using the ARM AESE/AESMC instructions.
void Aes128Encrypt(const uint8_t rk[11][16], const uint8_t in[16], uint8_t out[16]) {
  uint8x16_t state = vld1q_u8(in);
  for (int i = 0; i < 9; i++) {
    state = vaeseq_u8(state, vld1q_u8(rk[i]));  // AddRoundKey+SubBytes+ShiftRows
    state = vaesmcq_u8(state);                   // MixColumns
  }
  state = vaeseq_u8(state, vld1q_u8(rk[9]));     // last round: no MixColumns
  state = veorq_u8(state, vld1q_u8(rk[10]));     // final AddRoundKey
  vst1q_u8(out, state);
}

bool eq16(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 16) == 0; }

// Instruction-level inverse identities for the decrypt ops, independent of the
// (subtle) equivalent-inverse-cipher key schedule:
//   AESD(AESE(x, 0), 0) == x   -- InvSubBytes.InvShiftRows undoes ShiftRows.SubBytes
//   AESIMC(AESMC(x))    == x   -- InvMixColumns undoes MixColumns
bool AesdUndoesAese(const uint8_t x[16], uint8_t out[16]) {
  const uint8x16_t zero = vdupq_n_u8(0);
  uint8x16_t e = vaeseq_u8(vld1q_u8(x), zero);
  uint8x16_t d = vaesdq_u8(e, zero);
  vst1q_u8(out, d);
  return eq16(out, x);
}

bool AesimcUndoesAesmc(const uint8_t x[16], uint8_t out[16]) {
  uint8x16_t r = vaesimcq_u8(vaesmcq_u8(vld1q_u8(x)));
  vst1q_u8(out, r);
  return eq16(out, x);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloaes_MainActivity_probeAes(JNIEnv* env, jobject /*this*/) {
  std::string report = "ARMv8 AES crypto probe (FIPS-197 C.1):\n";
  char buf[256];

  const uint8_t key[16] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                           0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
  const uint8_t plain[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
                             0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  const uint8_t expect[16] = {0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
                              0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};

  uint8_t rk[11][16];
  ExpandKey128(key, rk);

  uint8_t cipher[16];
  Aes128Encrypt(rk, plain, cipher);
  bool enc_ok = eq16(cipher, expect);
  snprintf(buf, sizeof(buf), "  AESE/AESMC encrypt: %s\n", enc_ok ? "OK" : "FAIL");
  report += buf;

  uint8_t tmp[16];
  bool aesd_ok = AesdUndoesAese(plain, tmp);
  snprintf(buf, sizeof(buf), "  AESD inverts AESE: %s\n", aesd_ok ? "OK" : "FAIL");
  report += buf;
  if (!aesd_ok) {
    snprintf(buf, sizeof(buf), "    got=%02x%02x%02x%02x want=00112233\n",
             tmp[0], tmp[1], tmp[2], tmp[3]);
    report += buf;
  }

  bool aesimc_ok = AesimcUndoesAesmc(plain, tmp);
  snprintf(buf, sizeof(buf), "  AESIMC inverts AESMC: %s\n", aesimc_ok ? "OK" : "FAIL");
  report += buf;
  if (!aesimc_ok) {
    snprintf(buf, sizeof(buf), "    got=%02x%02x%02x%02x want=00112233\n",
             tmp[0], tmp[1], tmp[2], tmp[3]);
    report += buf;
  }

  if (!enc_ok) {
    snprintf(buf, sizeof(buf), "  enc got=%02x%02x%02x%02x... want=69c4e0d8...\n",
             cipher[0], cipher[1], cipher[2], cipher[3]);
    report += buf;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
