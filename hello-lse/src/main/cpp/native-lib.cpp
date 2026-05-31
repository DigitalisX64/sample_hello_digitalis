// Integration-level probes for the Armv8.1-LSE atomic family.
//
// Locks in the routing fixes (LDEOR/LDSET swap, and CAS/CASP
// dispatch swap).  Each probe is an *inline-asm
// emit* of one LSE opcode so the compiler can't rewrite it across -O
// levels or substitute a non-LSE LL/SC pair.  CMakeLists.txt builds with
// -march=armv8.1-a+lse for completeness.
//
// Probe coverage:
//   CAS    W / X         -- single-register compare-and-swap
//   CASP   W / X         -- pair compare-and-swap (regression target)
//   SWP    W / X         -- atomic exchange
//   LDADD  W / X         -- fetch-add
//   LDCLR  W / X         -- fetch-and-with-NOT-of-operand (regression target)
//   LDEOR  W / X         -- fetch-xor (regression target: swap with LDSET)
//   LDSET  W / X         -- fetch-or  (regression target: swap with LDEOR)
// LDSMAX / LDSMIN W -- signed max/min
// LDUMAX / LDUMIN W -- unsigned max/min
//
// For each: distinct inputs chosen so a silent dispatch swap between
// neighbouring opcodes produces a *different* memory or return value.

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellolse"

namespace {

// CAS W: w0 (Rs, expected) is read-write; w1 (Rt, desired) is read-only.
// On success the actual prior memory value is returned in Rs.
inline uint32_t cas_w(uint32_t* p, uint32_t expected, uint32_t desired) {
  __asm__ __volatile__("cas %w0, %w2, [%1]"
                       : "+r"(expected)
                       : "r"(p), "r"(desired)
                       : "memory");
  return expected;
}

inline uint64_t cas_x(uint64_t* p, uint64_t expected, uint64_t desired) {
  __asm__ __volatile__("cas %0, %2, [%1]"
                       : "+r"(expected)
                       : "r"(p), "r"(desired)
                       : "memory");
  return expected;
}

// CASP W: Ws/Ws+1 = expected_lo/hi (read-write); Wt/Wt+1 = desired_lo/hi.
// ARM requires consecutive even-numbered W registers -- pin to w0/w1 and
// w2/w3 explicitly so the encoding is unambiguous.
inline bool casp_w(uint32_t* p, uint32_t exp_lo, uint32_t exp_hi,
                   uint32_t des_lo, uint32_t des_hi, uint32_t* out_lo,
                   uint32_t* out_hi) {
  register uint32_t r0 asm("w0") = exp_lo;
  register uint32_t r1 asm("w1") = exp_hi;
  register uint32_t r2 asm("w2") = des_lo;
  register uint32_t r3 asm("w3") = des_hi;
  register uint32_t* rp asm("x4") = p;
  __asm__ __volatile__("casp w0, w1, w2, w3, [%4]"
                       : "+r"(r0), "+r"(r1)
                       : "r"(r2), "r"(r3), "r"(rp)
                       : "memory");
  *out_lo = r0;
  *out_hi = r1;
  return (r0 == exp_lo) && (r1 == exp_hi);
}

// CASP X: pair of 64-bit (128-bit total).  Pointer must be 16-byte aligned.
inline bool casp_x(uint64_t* p, uint64_t exp_lo, uint64_t exp_hi,
                   uint64_t des_lo, uint64_t des_hi, uint64_t* out_lo,
                   uint64_t* out_hi) {
  register uint64_t r0 asm("x0") = exp_lo;
  register uint64_t r1 asm("x1") = exp_hi;
  register uint64_t r2 asm("x2") = des_lo;
  register uint64_t r3 asm("x3") = des_hi;
  register uint64_t* rp asm("x4") = p;
  __asm__ __volatile__("casp x0, x1, x2, x3, [%4]"
                       : "+r"(r0), "+r"(r1)
                       : "r"(r2), "r"(r3), "r"(rp)
                       : "memory");
  *out_lo = r0;
  *out_hi = r1;
  return (r0 == exp_lo) && (r1 == exp_hi);
}

inline uint32_t swp_w(uint32_t* p, uint32_t v) {
  uint32_t old;
  __asm__ __volatile__("swp %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint64_t swp_x(uint64_t* p, uint64_t v) {
  uint64_t old;
  __asm__ __volatile__("swp %2, %0, [%1]" : "=r"(old) : "r"(p), "r"(v) : "memory");
  return old;
}

inline uint32_t ldadd_w(uint32_t* p, uint32_t v) {
  uint32_t old;
  __asm__ __volatile__("ldadd %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint64_t ldadd_x(uint64_t* p, uint64_t v) {
  uint64_t old;
  __asm__ __volatile__("ldadd %2, %0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint32_t ldclr_w(uint32_t* p, uint32_t v) {
  uint32_t old;
  __asm__ __volatile__("ldclr %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint64_t ldclr_x(uint64_t* p, uint64_t v) {
  uint64_t old;
  __asm__ __volatile__("ldclr %2, %0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint32_t ldeor_w(uint32_t* p, uint32_t v) {
  uint32_t old;
  __asm__ __volatile__("ldeor %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint64_t ldeor_x(uint64_t* p, uint64_t v) {
  uint64_t old;
  __asm__ __volatile__("ldeor %2, %0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint32_t ldset_w(uint32_t* p, uint32_t v) {
  uint32_t old;
  __asm__ __volatile__("ldset %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint64_t ldset_x(uint64_t* p, uint64_t v) {
  uint64_t old;
  __asm__ __volatile__("ldset %2, %0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline int32_t ldsmax_w(int32_t* p, int32_t v) {
  int32_t old;
  __asm__ __volatile__("ldsmax %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline int32_t ldsmin_w(int32_t* p, int32_t v) {
  int32_t old;
  __asm__ __volatile__("ldsmin %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint32_t ldumax_w(uint32_t* p, uint32_t v) {
  uint32_t old;
  __asm__ __volatile__("ldumax %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

inline uint32_t ldumin_w(uint32_t* p, uint32_t v) {
  uint32_t old;
  __asm__ __volatile__("ldumin %w2, %w0, [%1]"
                       : "=r"(old)
                       : "r"(p), "r"(v)
                       : "memory");
  return old;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellolse_MainActivity_probeLse(JNIEnv* env, jobject /*this*/) {
  std::string report = "Armv8.1 LSE atomic probe:\n";
  char buf[384];

  // --- CAS W (single-register, 32-bit) ---
  {
    uint32_t mem = 0xAAAA;
    uint32_t prior_eq = cas_w(&mem, 0xAAAA, 0xBBBB);
    bool eq_ok = (prior_eq == 0xAAAA) && (mem == 0xBBBB);

    mem = 0xCCCC;
    uint32_t prior_ne = cas_w(&mem, 0x1234, 0x5678);
    bool ne_ok = (prior_ne == 0xCCCC) && (mem == 0xCCCC);

    snprintf(buf, sizeof(buf), "  CAS  W: eq=%s ne=%s\n",
             eq_ok ? "OK" : "FAIL", ne_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- CAS X (single-register, 64-bit) ---
  {
    uint64_t mem = 0x1122334455667788ULL;
    uint64_t prior_eq = cas_x(&mem, 0x1122334455667788ULL, 0xDEADBEEFCAFEBABEULL);
    bool eq_ok = (prior_eq == 0x1122334455667788ULL) &&
                 (mem == 0xDEADBEEFCAFEBABEULL);

    mem = 0xAABBCCDDEEFF0011ULL;
    uint64_t prior_ne = cas_x(&mem, 0x0, 0xFFFFFFFFFFFFFFFFULL);
    bool ne_ok = (prior_ne == 0xAABBCCDDEEFF0011ULL) &&
                 (mem == 0xAABBCCDDEEFF0011ULL);

    snprintf(buf, sizeof(buf), "  CAS  X: eq=%s ne=%s\n",
             eq_ok ? "OK" : "FAIL", ne_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- CASP W (pair of 32-bit, 8-byte aligned) ---
  // This is the regression target for CASP: prior decoder routed
  // o1=1 universally to kCas, miscompiling CASP as a single-register CAS.
  {
    alignas(8) uint32_t pair[2] = {0x1111, 0x2222};
    uint32_t out_lo = 0, out_hi = 0;
    bool swapped = casp_w(pair, /*exp_lo*/ 0x1111, /*exp_hi*/ 0x2222,
                          /*des_lo*/ 0xAAAA, /*des_hi*/ 0xBBBB,
                          &out_lo, &out_hi);
    bool eq_ok = swapped && (pair[0] == 0xAAAA) && (pair[1] == 0xBBBB) &&
                 (out_lo == 0x1111) && (out_hi == 0x2222);

    alignas(8) uint32_t pair2[2] = {0x3333, 0x4444};
    bool swapped2 = casp_w(pair2, /*exp_lo*/ 0xDEAD, /*exp_hi*/ 0xBEEF,
                           /*des_lo*/ 0xFFFF, /*des_hi*/ 0xFFFF,
                           &out_lo, &out_hi);
    bool ne_ok = (!swapped2) && (pair2[0] == 0x3333) && (pair2[1] == 0x4444) &&
                 (out_lo == 0x3333) && (out_hi == 0x4444);

    snprintf(buf, sizeof(buf), "  CASP W: eq=%s ne=%s\n",
             eq_ok ? "OK" : "FAIL", ne_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- CASP X (pair of 64-bit, 16-byte aligned, 128-bit data) ---
  {
    alignas(16) uint64_t pair[2] = {0x1111111111111111ULL, 0x2222222222222222ULL};
    uint64_t out_lo = 0, out_hi = 0;
    bool swapped = casp_x(pair,
                          /*exp_lo*/ 0x1111111111111111ULL,
                          /*exp_hi*/ 0x2222222222222222ULL,
                          /*des_lo*/ 0xAAAAAAAAAAAAAAAAULL,
                          /*des_hi*/ 0xBBBBBBBBBBBBBBBBULL,
                          &out_lo, &out_hi);
    bool eq_ok = swapped && (pair[0] == 0xAAAAAAAAAAAAAAAAULL) &&
                 (pair[1] == 0xBBBBBBBBBBBBBBBBULL) &&
                 (out_lo == 0x1111111111111111ULL) &&
                 (out_hi == 0x2222222222222222ULL);

    alignas(16) uint64_t pair2[2] = {0x3333333333333333ULL,
                                      0x4444444444444444ULL};
    bool swapped2 = casp_x(pair2,
                           /*exp_lo*/ 0xDEADDEADDEADDEADULL,
                           /*exp_hi*/ 0xBEEFBEEFBEEFBEEFULL,
                           /*des_lo*/ 0xFFFFFFFFFFFFFFFFULL,
                           /*des_hi*/ 0xFFFFFFFFFFFFFFFFULL,
                           &out_lo, &out_hi);
    bool ne_ok = (!swapped2) && (pair2[0] == 0x3333333333333333ULL) &&
                 (pair2[1] == 0x4444444444444444ULL) &&
                 (out_lo == 0x3333333333333333ULL) &&
                 (out_hi == 0x4444444444444444ULL);

    snprintf(buf, sizeof(buf), "  CASP X: eq=%s ne=%s\n",
             eq_ok ? "OK" : "FAIL", ne_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- SWP W / X ---
  {
    uint32_t mem_w = 0x1111;
    uint32_t old_w = swp_w(&mem_w, 0x2222);
    bool w_ok = (old_w == 0x1111) && (mem_w == 0x2222);

    uint64_t mem_x = 0x3333333333333333ULL;
    uint64_t old_x = swp_x(&mem_x, 0x4444444444444444ULL);
    bool x_ok =
        (old_x == 0x3333333333333333ULL) && (mem_x == 0x4444444444444444ULL);

    snprintf(buf, sizeof(buf), "  SWP   : W=%s X=%s\n", w_ok ? "OK" : "FAIL",
             x_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDADD W / X ---
  {
    uint32_t mem_w = 100;
    uint32_t old_w = ldadd_w(&mem_w, 23);
    bool w_ok = (old_w == 100) && (mem_w == 123);

    uint64_t mem_x = 0x1000000000ULL;
    uint64_t old_x = ldadd_x(&mem_x, 0x100ULL);
    bool x_ok = (old_x == 0x1000000000ULL) && (mem_x == 0x1000000100ULL);

    snprintf(buf, sizeof(buf), "  LDADD : W=%s X=%s\n", w_ok ? "OK" : "FAIL",
             x_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDCLR W / X --- (regression target: LDEOR/LDSET swap context)
  // LDCLR semantics: mem' = mem AND NOT(Rs).  Pick mask = 0xFF00 so the
  // result differs from LDSET (OR) and LDEOR (XOR).
  {
    uint32_t mem_w = 0x0F0F;
    uint32_t old_w = ldclr_w(&mem_w, 0xFF00);
    // 0x0F0F & ~0xFF00 = 0x0F0F & 0xFFFF00FF = 0x000F
    bool w_ok = (old_w == 0x0F0F) && (mem_w == 0x000F);

    uint64_t mem_x = 0x0F0F0F0F0F0F0F0FULL;
    uint64_t old_x = ldclr_x(&mem_x, 0xFFFF0000FFFF0000ULL);
    // 0x0F0F0F0F0F0F0F0F & ~0xFFFF0000FFFF0000 = 0x00000F0F00000F0F
    bool x_ok =
        (old_x == 0x0F0F0F0F0F0F0F0FULL) && (mem_x == 0x00000F0F00000F0FULL);

    snprintf(buf, sizeof(buf), "  LDCLR : W=%s X=%s\n", w_ok ? "OK" : "FAIL",
             x_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDEOR W / X --- (regression target)
  {
    uint32_t mem_w = 0x0F0F;
    uint32_t old_w = ldeor_w(&mem_w, 0xFF00);
    // 0x0F0F XOR 0xFF00 = 0xF00F
    bool w_ok = (old_w == 0x0F0F) && (mem_w == 0xF00F);

    uint64_t mem_x = 0x0F0F0F0F0F0F0F0FULL;
    uint64_t old_x = ldeor_x(&mem_x, 0xFFFF0000FFFF0000ULL);
    // XOR -> 0xF0F00F0FF0F00F0F
    bool x_ok =
        (old_x == 0x0F0F0F0F0F0F0F0FULL) && (mem_x == 0xF0F00F0FF0F00F0FULL);

    snprintf(buf, sizeof(buf), "  LDEOR : W=%s X=%s\n", w_ok ? "OK" : "FAIL",
             x_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDSET W / X --- (regression target — was swapped with LDEOR)
  {
    uint32_t mem_w = 0x0F0F;
    uint32_t old_w = ldset_w(&mem_w, 0xFF00);
    // 0x0F0F OR 0xFF00 = 0xFF0F (distinct from LDEOR's 0xF00F)
    bool w_ok = (old_w == 0x0F0F) && (mem_w == 0xFF0F);

    uint64_t mem_x = 0x0F0F0F0F0F0F0F0FULL;
    uint64_t old_x = ldset_x(&mem_x, 0xFFFF0000FFFF0000ULL);
    // OR -> 0xFFFF0F0FFFFF0F0F
    bool x_ok =
        (old_x == 0x0F0F0F0F0F0F0F0FULL) && (mem_x == 0xFFFF0F0FFFFF0F0FULL);

    snprintf(buf, sizeof(buf), "  LDSET : W=%s X=%s\n", w_ok ? "OK" : "FAIL",
             x_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDSMAX / LDSMIN W (signed) ---
  // mem = -1 (0xFFFFFFFF), op = +1: SMAX picks +1; SMIN picks -1.
  // The signed-vs-unsigned interpretation differs from LDUMAX/LDUMIN below.
  {
    int32_t mem_max = -1;
    int32_t old_max = ldsmax_w(&mem_max, 1);
    bool max_ok = (old_max == -1) && (mem_max == 1);

    int32_t mem_min = -1;
    int32_t old_min = ldsmin_w(&mem_min, 1);
    bool min_ok = (old_min == -1) && (mem_min == -1);

    snprintf(buf, sizeof(buf), "  LDS-W : MAX=%s MIN=%s\n",
             max_ok ? "OK" : "FAIL", min_ok ? "OK" : "FAIL");
    report += buf;
  }

  // --- LDUMAX / LDUMIN W (unsigned) ---
  // mem = 0xFFFFFFFF (unsigned max), op = 1: UMAX picks 0xFFFFFFFF; UMIN picks 1.
  {
    uint32_t mem_max = 0xFFFFFFFFu;
    uint32_t old_max = ldumax_w(&mem_max, 1u);
    bool max_ok = (old_max == 0xFFFFFFFFu) && (mem_max == 0xFFFFFFFFu);

    uint32_t mem_min = 0xFFFFFFFFu;
    uint32_t old_min = ldumin_w(&mem_min, 1u);
    bool min_ok = (old_min == 0xFFFFFFFFu) && (mem_min == 1u);

    snprintf(buf, sizeof(buf), "  LDU-W : MAX=%s MIN=%s\n",
             max_ok ? "OK" : "FAIL", min_ok ? "OK" : "FAIL");
    report += buf;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
