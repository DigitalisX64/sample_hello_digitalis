// Integration-level probes for the ARMv8 load/store-exclusive PAIR family:
// LDXP / LDAXP / STXP / STLXP.
//
// These are the LL/SC exclusive-monitor counterpart to the LSE atomics in
// hello-lse. They share the o1=1, o2=0 encoding slot with CASP and are
// distinguished only by bit31; a decoder that ignored bit31 mis-decoded the
// pair form as a single-register CASP. Each probe is an inline-asm emit of one
// exclusive-pair opcode so the compiler can't rewrite it across -O levels.
//
// Probe coverage:
//   LDXP  / STXP   W (32-bit pair)  -- relaxed exclusive pair
//   LDXP  / STXP   X (64-bit pair, 128-bit data)
//   LDAXP / STLXP  W (32-bit pair)  -- acquire/release exclusive pair
//   LDAXP / STLXP  X (64-bit pair)

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "helloldxp"

namespace {

// 32-bit exclusive pair, relaxed. Loads the current pair (into *got_lo/_hi),
// then publishes {new_lo, new_hi} with a STXP retry loop. Returns once the
// store succeeds. p points at two contiguous 32-bit words.
void ldxp_stxp_w(uint32_t* p, uint32_t new_lo, uint32_t new_hi,
                 uint32_t* got_lo, uint32_t* got_hi) {
  uint32_t lo, hi, status;
  do {
    __asm__ __volatile__("ldxp %w0, %w1, [%2]"
                         : "=&r"(lo), "=&r"(hi)
                         : "r"(p)
                         : "memory");
    __asm__ __volatile__("stxp %w0, %w2, %w3, [%1]"
                         : "=&r"(status)
                         : "r"(p), "r"(new_lo), "r"(new_hi)
                         : "memory");
  } while (status != 0);
  *got_lo = lo;
  *got_hi = hi;
}

// 64-bit exclusive pair, relaxed (128-bit data; p must be 16-byte aligned).
void ldxp_stxp_x(uint64_t* p, uint64_t new_lo, uint64_t new_hi,
                 uint64_t* got_lo, uint64_t* got_hi) {
  uint64_t lo, hi;
  uint32_t status;
  do {
    __asm__ __volatile__("ldxp %0, %1, [%2]"
                         : "=&r"(lo), "=&r"(hi)
                         : "r"(p)
                         : "memory");
    __asm__ __volatile__("stxp %w0, %2, %3, [%1]"
                         : "=&r"(status)
                         : "r"(p), "r"(new_lo), "r"(new_hi)
                         : "memory");
  } while (status != 0);
  *got_lo = lo;
  *got_hi = hi;
}

// Acquire/release variants: LDAXP + STLXP (32-bit pair).
void ldaxp_stlxp_w(uint32_t* p, uint32_t new_lo, uint32_t new_hi,
                   uint32_t* got_lo, uint32_t* got_hi) {
  uint32_t lo, hi, status;
  do {
    __asm__ __volatile__("ldaxp %w0, %w1, [%2]"
                         : "=&r"(lo), "=&r"(hi)
                         : "r"(p)
                         : "memory");
    __asm__ __volatile__("stlxp %w0, %w2, %w3, [%1]"
                         : "=&r"(status)
                         : "r"(p), "r"(new_lo), "r"(new_hi)
                         : "memory");
  } while (status != 0);
  *got_lo = lo;
  *got_hi = hi;
}

// Acquire/release variants: LDAXP + STLXP (64-bit pair).
void ldaxp_stlxp_x(uint64_t* p, uint64_t new_lo, uint64_t new_hi,
                   uint64_t* got_lo, uint64_t* got_hi) {
  uint64_t lo, hi;
  uint32_t status;
  do {
    __asm__ __volatile__("ldaxp %0, %1, [%2]"
                         : "=&r"(lo), "=&r"(hi)
                         : "r"(p)
                         : "memory");
    __asm__ __volatile__("stlxp %w0, %2, %3, [%1]"
                         : "=&r"(status)
                         : "r"(p), "r"(new_lo), "r"(new_hi)
                         : "memory");
  } while (status != 0);
  *got_lo = lo;
  *got_hi = hi;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloldxp_MainActivity_probeLdxp(JNIEnv* env, jobject /*this*/) {
  std::string report = "ARMv8 load/store-exclusive pair probe:\n";
  char buf[256];

  // --- LDXP / STXP W (32-bit pair) ---
  {
    alignas(8) uint32_t mem[2] = {0xAAAA1111u, 0xBBBB2222u};
    uint32_t got_lo = 0, got_hi = 0;
    ldxp_stxp_w(mem, 0xCAFE0001u, 0xCAFE0002u, &got_lo, &got_hi);
    bool ok = (got_lo == 0xAAAA1111u) && (got_hi == 0xBBBB2222u) &&
              (mem[0] == 0xCAFE0001u) && (mem[1] == 0xCAFE0002u);
    snprintf(buf, sizeof(buf), "  LDXP/STXP   W: %s\n", ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDXP / STXP X (64-bit pair, 128-bit data) ---
  {
    alignas(16) uint64_t mem[2] = {0x1111222233334444ULL, 0xAAAABBBBCCCCDDDDULL};
    uint64_t got_lo = 0, got_hi = 0;
    ldxp_stxp_x(mem, 0xDEADBEEF00000001ULL, 0xF00DCAFE00000002ULL, &got_lo,
                &got_hi);
    bool ok = (got_lo == 0x1111222233334444ULL) &&
              (got_hi == 0xAAAABBBBCCCCDDDDULL) &&
              (mem[0] == 0xDEADBEEF00000001ULL) &&
              (mem[1] == 0xF00DCAFE00000002ULL);
    snprintf(buf, sizeof(buf), "  LDXP/STXP   X: %s\n", ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDAXP / STLXP W (acquire/release, 32-bit pair) ---
  {
    alignas(8) uint32_t mem[2] = {0x01234567u, 0x89ABCDEFu};
    uint32_t got_lo = 0, got_hi = 0;
    ldaxp_stlxp_w(mem, 0x11111111u, 0x22222222u, &got_lo, &got_hi);
    bool ok = (got_lo == 0x01234567u) && (got_hi == 0x89ABCDEFu) &&
              (mem[0] == 0x11111111u) && (mem[1] == 0x22222222u);
    snprintf(buf, sizeof(buf), "  LDAXP/STLXP W: %s\n", ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDAXP / STLXP X (acquire/release, 64-bit pair) ---
  {
    alignas(16) uint64_t mem[2] = {0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL};
    uint64_t got_lo = 0, got_hi = 0;
    ldaxp_stlxp_x(mem, 0x1111111111111111ULL, 0x2222222222222222ULL, &got_lo,
                  &got_hi);
    bool ok = (got_lo == 0x0123456789ABCDEFULL) &&
              (got_hi == 0xFEDCBA9876543210ULL) &&
              (mem[0] == 0x1111111111111111ULL) &&
              (mem[1] == 0x2222222222222222ULL);
    snprintf(buf, sizeof(buf), "  LDAXP/STLXP X: %s\n", ok ? "OK" : "FAIL");
    report += buf;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
