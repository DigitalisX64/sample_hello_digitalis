// Integration-level probe for the LSE atomics the base probe doesn't reach:
// the BYTE and HALFWORD fetch-and-op forms (LDADDB/LDSETB/LDCLRB/LDEORB,
// LDSMAXB/LDSMINH/LDUMAXH — with signed-vs-unsigned distinguishing values and
// zero-extension checks), the 64-bit CASP pair, and the LDXP/STXP
// pair-exclusive read-modify-write loop in both pair widths. Each op is
// emitted via inline asm and run in a HOT LOOP past the JIT gear-up threshold
// so the heavy tier's sized-CMPXCHG and CMPXCHG16B lowerings are exercised;
// every returned old value and resulting memory is checked and a mismatch
// aborts() (SIGABRT).

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#define LOG_TAG "hellolsepair"

namespace {

constexpr int kHotIters = 4000;  // > gear-up threshold (1000)

#define DEF_LSE_B(NAME, INSN)                                              \
  __attribute__((noinline)) uint32_t NAME(uint8_t* mem, uint32_t val) {    \
    uint32_t old;                                                          \
    __asm__ volatile(INSN " %w2, %w0, [%1]"                                \
                     : "=r"(old)                                           \
                     : "r"(mem), "r"(val)                                  \
                     : "memory");                                          \
    return old;                                                            \
  }
DEF_LSE_B(LseAddB, "ldaddb")
DEF_LSE_B(LseSetB, "ldsetb")
DEF_LSE_B(LseClrB, "ldclrb")
DEF_LSE_B(LseEorB, "ldeorb")
DEF_LSE_B(LseSmaxB, "ldsmaxb")
DEF_LSE_B(LseUmaxB, "ldumaxb")
#undef DEF_LSE_B

#define DEF_LSE_H(NAME, INSN)                                              \
  __attribute__((noinline)) uint32_t NAME(uint16_t* mem, uint32_t val) {   \
    uint32_t old;                                                          \
    __asm__ volatile(INSN " %w2, %w0, [%1]"                                \
                     : "=r"(old)                                           \
                     : "r"(mem), "r"(val)                                  \
                     : "memory");                                          \
    return old;                                                            \
  }
DEF_LSE_H(LseSmaxH, "ldsmaxh")
DEF_LSE_H(LseSminH, "ldsminh")
DEF_LSE_H(LseUminH, "lduminh")
DEF_LSE_H(LseEorH, "ldeorh")
#undef DEF_LSE_H

// CASP 64-bit pair: expected in X0:X1, desired in X2:X3, old pair returned in
// X0:X1 (consecutive even/odd registers required).
__attribute__((noinline)) void Casp64(uint64_t* mem, uint64_t e0, uint64_t e1,
                                      uint64_t n0, uint64_t n1, uint64_t* old0,
                                      uint64_t* old1) {
  register uint64_t s0 __asm__("x0") = e0;
  register uint64_t s1 __asm__("x1") = e1;
  register uint64_t t2 __asm__("x2") = n0;
  register uint64_t t3 __asm__("x3") = n1;
  register uint64_t* addr __asm__("x4") = mem;
  __asm__ volatile("casp x0, x1, x2, x3, [x4]"
                   : "+r"(s0), "+r"(s1)
                   : "r"(t2), "r"(t3), "r"(addr)
                   : "memory");
  *old0 = s0;
  *old1 = s1;
}

// LDXP/STXP 64-bit pair RMW loop: mem.lo += 3, mem.hi += 7 atomically.
__attribute__((noinline)) void XpAdd64(uint64_t* mem) {
  uint64_t lo, hi;
  uint32_t status;
  __asm__ volatile(
      "1:\n\t"
      "ldxp %0, %1, [%3]\n\t"
      "add %0, %0, #3\n\t"
      "add %1, %1, #7\n\t"
      "stxp %w2, %0, %1, [%3]\n\t"
      "cbnz %w2, 1b"
      : "=&r"(lo), "=&r"(hi), "=&r"(status)
      : "r"(mem)
      : "memory");
}

// LDXP/STXP 32-bit pair RMW loop: mem.lo += 5, mem.hi += 11 atomically.
__attribute__((noinline)) void XpAdd32(uint32_t* mem) {
  uint32_t lo, hi, status;
  __asm__ volatile(
      "1:\n\t"
      "ldxp %w0, %w1, [%3]\n\t"
      "add %w0, %w0, #5\n\t"
      "add %w1, %w1, #11\n\t"
      "stxp %w2, %w0, %w1, [%3]\n\t"
      "cbnz %w2, 1b"
      : "=&r"(lo), "=&r"(hi), "=&r"(status)
      : "r"(mem)
      : "memory");
}

bool RunChecks() {
  for (int i = 0; i < kHotIters; i++) {
    // Byte forms: old value must come back zero-extended; memory truncates.
    uint8_t b = 0xF0;
    if (LseAddB(&b, 0x1B) != 0xF0 || b != 0x0B) return false;  // 0xF0+0x1B = 0x10B -> 0x0B
    b = 0x0F;
    if (LseSetB(&b, 0xA0) != 0x0F || b != 0xAF) return false;
    b = 0xFF;
    if (LseClrB(&b, 0x0F) != 0xFF || b != 0xF0) return false;
    b = 0xAA;
    if (LseEorB(&b, 0xFF) != 0xAA || b != 0x55) return false;
    // Signed byte max: 0xFF is -1 signed, so max(-1, 5) = 5.
    b = 0xFF;
    if (LseSmaxB(&b, 5) != 0xFF || b != 0x05) return false;
    // Unsigned byte max: 0xFF is 255 unsigned, so max stays 0xFF.
    b = 0xFF;
    if (LseUmaxB(&b, 5) != 0xFF || b != 0xFF) return false;

    // Halfword forms. 0x8000 is -32768 signed / 32768 unsigned.
    uint16_t h = 0x8000;
    if (LseSmaxH(&h, 7) != 0x8000 || h != 0x0007) return false;
    h = 0x8000;
    if (LseSminH(&h, 7) != 0x8000 || h != 0x8000) return false;
    h = 0x8000;
    if (LseUminH(&h, 7) != 0x8000 || h != 0x0007) return false;
    h = 0x1234;
    if (LseEorH(&h, 0xFFFF) != 0x1234 || h != 0xEDCB) return false;

    // CASP-64 match: swap happens, old == expected.
    alignas(16) uint64_t pair[2] = {0x1111111111111111ULL + i, 0x2222222222222222ULL};
    uint64_t o0, o1;
    Casp64(pair, pair[0], pair[1], 0xAAAAAAAAAAAAAAAAULL, 0xBBBBBBBBBBBBBBBBULL, &o0, &o1);
    if (o0 != 0x1111111111111111ULL + i || o1 != 0x2222222222222222ULL ||
        pair[0] != 0xAAAAAAAAAAAAAAAAULL || pair[1] != 0xBBBBBBBBBBBBBBBBULL) {
      return false;
    }
    // CASP-64 mismatch: no swap, old == actual memory.
    alignas(16) uint64_t pair2[2] = {0xDEADBEEFULL, 0xCAFEBABEULL};
    Casp64(pair2, 1, 2, 3, 4, &o0, &o1);
    if (o0 != 0xDEADBEEFULL || o1 != 0xCAFEBABEULL || pair2[0] != 0xDEADBEEFULL ||
        pair2[1] != 0xCAFEBABEULL) {
      return false;
    }

    // Pair-exclusive RMW loops.
    alignas(16) uint64_t q[2] = {100, 200};
    XpAdd64(q);
    if (q[0] != 103 || q[1] != 207) return false;
    alignas(8) uint32_t w[2] = {40, 50};
    XpAdd32(w);
    if (w[0] != 45 || w[1] != 61) return false;
  }
  return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellolsepair_MainActivity_probeLsepair(JNIEnv* env, jobject /*this*/) {
  const bool ok = RunChecks();
  char buf[192];
  snprintf(buf, sizeof(buf),
           "LSE byte/half + CASP-64 + LDXP/STXP pair probe x%d: %s\n",
           kHotIters, ok ? "OK" : "FAIL");
  __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, LOG_TAG, "%s", buf);
  if (!ok) abort();
  return env->NewStringUTF(buf);
}
