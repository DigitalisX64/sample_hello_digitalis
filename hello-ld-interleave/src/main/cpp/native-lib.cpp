// Exercises NEON multi-structure load/store: LD1/ST1 multi-reg
// (contiguous, no de-interleave) AND true LD2/ST2 (element-interleaved).
// Regression for the decoder split landed against
// DigitalisX64/platform_frameworks_libs_binary_translation#1.
//
// Before the fix, the decoder mapped both opcode families to the same
// "contiguous" interpretation, so LD2 silently produced wrong data --
// which manifested as the Facebook superpack decompressor's integrity
// check failing.

#include <arm_neon.h>
#include <android/log.h>
#include <jni.h>

#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "helloldinterleave"

namespace {

// LD2 .16B: read 32 interleaved bytes, get two vectors of 16.
// Memory layout: [a0, b0, a1, b1, ..., a15, b15]
// After LD2:    V[rt]   = {a0, a1, ..., a15}
//               V[rt+1] = {b0, b1, ..., b15}
bool test_ld2_b(const uint8_t* mem) {
  uint8x16x2_t v = vld2q_u8(mem);
  for (int i = 0; i < 16; i++) {
    if (v.val[0][i] != mem[2 * i + 0]) return false;
    if (v.val[1][i] != mem[2 * i + 1]) return false;
  }
  return true;
}

// LD2 .4S: read 8 interleaved 32-bit words.
bool test_ld2_s(const uint32_t* mem) {
  uint32x4x2_t v = vld2q_u32(mem);
  for (int i = 0; i < 4; i++) {
    if (v.val[0][i] != mem[2 * i + 0]) return false;
    if (v.val[1][i] != mem[2 * i + 1]) return false;
  }
  return true;
}

// ST2 .16B: write two vectors interleaved into 32 bytes of memory.
bool test_st2_b(uint8_t* dst, const uint8_t* a16, const uint8_t* b16) {
  uint8x16x2_t v;
  v.val[0] = vld1q_u8(a16);
  v.val[1] = vld1q_u8(b16);
  vst2q_u8(dst, v);
  for (int i = 0; i < 16; i++) {
    if (dst[2 * i + 0] != a16[i]) return false;
    if (dst[2 * i + 1] != b16[i]) return false;
  }
  return true;
}

// ST3 .16B: write three vectors interleaved into 48 bytes of memory.
bool test_st3_b(uint8_t* dst, const uint8_t* a16, const uint8_t* b16,
                const uint8_t* c16) {
  uint8x16x3_t v;
  v.val[0] = vld1q_u8(a16);
  v.val[1] = vld1q_u8(b16);
  v.val[2] = vld1q_u8(c16);
  vst3q_u8(dst, v);
  for (int i = 0; i < 16; i++) {
    if (dst[3 * i + 0] != a16[i]) return false;
    if (dst[3 * i + 1] != b16[i]) return false;
    if (dst[3 * i + 2] != c16[i]) return false;
  }
  return true;
}

// ST4 .16B: write four vectors interleaved into 64 bytes of memory (RGBA).
bool test_st4_b(uint8_t* dst, const uint8_t* a16, const uint8_t* b16,
                const uint8_t* c16, const uint8_t* d16) {
  uint8x16x4_t v;
  v.val[0] = vld1q_u8(a16);
  v.val[1] = vld1q_u8(b16);
  v.val[2] = vld1q_u8(c16);
  v.val[3] = vld1q_u8(d16);
  vst4q_u8(dst, v);
  for (int i = 0; i < 16; i++) {
    if (dst[4 * i + 0] != a16[i]) return false;
    if (dst[4 * i + 1] != b16[i]) return false;
    if (dst[4 * i + 2] != c16[i]) return false;
    if (dst[4 * i + 3] != d16[i]) return false;
  }
  return true;
}

// LD3 .16B: read 48 interleaved bytes, get three vectors.
bool test_ld3_b(const uint8_t* mem) {
  uint8x16x3_t v = vld3q_u8(mem);
  for (int i = 0; i < 16; i++) {
    if (v.val[0][i] != mem[3 * i + 0]) return false;
    if (v.val[1][i] != mem[3 * i + 1]) return false;
    if (v.val[2][i] != mem[3 * i + 2]) return false;
  }
  return true;
}

// LD4 .16B: read 64 interleaved bytes, get four vectors.
bool test_ld4_b(const uint8_t* mem) {
  uint8x16x4_t v = vld4q_u8(mem);
  for (int i = 0; i < 16; i++) {
    if (v.val[0][i] != mem[4 * i + 0]) return false;
    if (v.val[1][i] != mem[4 * i + 1]) return false;
    if (v.val[2][i] != mem[4 * i + 2]) return false;
    if (v.val[3][i] != mem[4 * i + 3]) return false;
  }
  return true;
}

// LD1 with 2 contiguous registers -- should NOT de-interleave. Distinct
// opcode (0b1010) from LD2 (0b1000). Bionic's NEON memcpy / strcmp
// emit this; getting it wrong silently broke libc string ops.
bool test_ld1x2_b(const uint8_t* mem) {
  uint8x16x2_t v = vld1q_u8_x2(mem);
  for (int i = 0; i < 16; i++) {
    if (v.val[0][i] != mem[i])      return false;
    if (v.val[1][i] != mem[16 + i]) return false;
  }
  return true;
}

// LDTRSB/LDTRSH/LDTRSW -- unprivileged signed loads. At EL0 these are LDUR-
// equivalent and must decode and sign-extend. The decoder previously ignored
// bit21 at op4=0b10 and routed both register-offset and unprivileged encodings
// to the register-offset handler, so LDTR*/STTR* were misdecoded -- either
// Undefined (SIGILL) or a wrong-address load. Emitted via inline asm because
// the compiler does not generate LDTR for ordinary code.
bool test_ldtr() {
  alignas(8) uint8_t buf[16];
  memset(buf, 0, sizeof(buf));
  buf[1] = 0x80;                            // ldtrsb [buf,#1] -> sign-extend
  buf[2] = 0x00; buf[3] = 0x80;             // ldtrsh [buf,#2] = 0x8000
  buf[4] = 0; buf[5] = 0; buf[6] = 0; buf[7] = 0x80;  // ldtrsw [buf,#4] = 0x80000000
  uint64_t base = reinterpret_cast<uint64_t>(buf);
  int64_t b8 = 0, h16 = 0, w32 = 0;
  asm volatile("ldtrsb %0, [%1, #1]" : "=r"(b8) : "r"(base) : "memory");
  asm volatile("ldtrsh %0, [%1, #2]" : "=r"(h16) : "r"(base) : "memory");
  asm volatile("ldtrsw %0, [%1, #4]" : "=r"(w32) : "r"(base) : "memory");
  return b8 == static_cast<int64_t>(0xFFFFFFFFFFFFFF80ULL) &&
         h16 == static_cast<int64_t>(0xFFFFFFFFFFFF8000ULL) &&
         w32 == static_cast<int64_t>(0xFFFFFFFF80000000ULL);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloldinterleave_MainActivity_probeLdInterleave(
    JNIEnv* env, jobject /*this*/) {
  alignas(16) uint8_t buf[64];
  for (int i = 0; i < 64; i++) buf[i] = static_cast<uint8_t>(i);

  alignas(16) uint32_t buf_s[8];
  for (int i = 0; i < 8; i++) buf_s[i] = 0x1000 + i;

  alignas(16) uint8_t a16[16], b16[16], c16[16], d16[16], st_dst[64];
  for (int i = 0; i < 16; i++) {
    a16[i] = 0xA0 + i;
    b16[i] = 0xB0 + i;
    c16[i] = 0xC0 + i;
    d16[i] = 0xD0 + i;
  }

  bool ld2b  = test_ld2_b(buf);
  bool ld2s  = test_ld2_s(buf_s);
  bool st2b  = test_st2_b(st_dst, a16, b16);
  bool ld3b  = test_ld3_b(buf);
  bool ld4b  = test_ld4_b(buf);
  bool st3b  = test_st3_b(st_dst, a16, b16, c16);
  bool st4b  = test_st4_b(st_dst, a16, b16, c16, d16);
  bool ld1x2 = test_ld1x2_b(buf);
  bool ldtr  = test_ldtr();

  char msg[320];
  snprintf(msg, sizeof(msg),
           "NEON multi-struct probe:\n"
           "  LD2.16B=%s LD2.4S=%s ST2.16B=%s\n"
           "  LD3.16B=%s LD4.16B=%s ST3.16B=%s ST4.16B=%s\n"
           "  LD1.16B x2=%s (contiguous, must not de-interleave)\n"
           "  LDTRSB/H/SW=%s (unprivileged signed loads, sign-extend)",
           ld2b  ? "OK" : "FAIL", ld2s  ? "OK" : "FAIL", st2b  ? "OK" : "FAIL",
           ld3b  ? "OK" : "FAIL", ld4b  ? "OK" : "FAIL", st3b ? "OK" : "FAIL",
           st4b ? "OK" : "FAIL", ld1x2 ? "OK" : "FAIL", ldtr ? "OK" : "FAIL");
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg);
  return env->NewStringUTF(msg);
}
