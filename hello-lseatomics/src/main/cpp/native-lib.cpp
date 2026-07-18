// Integration-level probe for the ARMv8.1 LSE (Large System Extensions) atomic
// read-modify-write instructions: LDSET/LDCLR/LDEOR (bitwise fetch-and-op),
// LDSMAX/LDSMIN/LDUMAX/LDUMIN (atomic min/max) and CASP (compare-and-swap pair).
//
// These are what clang emits for std::atomic fetch_or/and/xor and the lock-free
// containers real apps use everywhere; a mis-translation silently corrupts
// lock-free data. Each op is emitted via inline asm (so the exact encoding is
// under test) and run in a HOT LOOP past the JIT gear-up threshold, so the heavy
// optimizer lowers it. Every iteration checks the returned old value and the
// resulting memory against a scalar reference; a mismatch aborts() (SIGABRT) and
// an unimplemented encoding SIGILLs — either way the sample suite flags it.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define LOG_TAG "hellolse"

namespace {

constexpr int kHotIters = 4000;  // > config::kGearSwitchThreshold (1000)

// Each op keeps to its own noinline callee so its region gears up independently.
__attribute__((noinline)) uint64_t do_ldset(uint64_t* mem, uint64_t val) {
  uint64_t old;
  __asm__ volatile("ldset %2, %0, [%1]"
                   : "=r"(old)
                   : "r"(mem), "r"(val)
                   : "memory");
  return old;
}
__attribute__((noinline)) uint64_t do_ldclr(uint64_t* mem, uint64_t val) {
  uint64_t old;
  __asm__ volatile("ldclr %2, %0, [%1]"
                   : "=r"(old)
                   : "r"(mem), "r"(val)
                   : "memory");
  return old;
}
__attribute__((noinline)) uint64_t do_ldeor(uint64_t* mem, uint64_t val) {
  uint64_t old;
  __asm__ volatile("ldeor %2, %0, [%1]"
                   : "=r"(old)
                   : "r"(mem), "r"(val)
                   : "memory");
  return old;
}
__attribute__((noinline)) uint64_t do_ldsmax(uint64_t* mem, uint64_t val) {
  uint64_t old;
  __asm__ volatile("ldsmax %2, %0, [%1]"
                   : "=r"(old)
                   : "r"(mem), "r"(val)
                   : "memory");
  return old;
}
__attribute__((noinline)) uint64_t do_ldsmin(uint64_t* mem, uint64_t val) {
  uint64_t old;
  __asm__ volatile("ldsmin %2, %0, [%1]"
                   : "=r"(old)
                   : "r"(mem), "r"(val)
                   : "memory");
  return old;
}
__attribute__((noinline)) uint64_t do_ldumax(uint64_t* mem, uint64_t val) {
  uint64_t old;
  __asm__ volatile("ldumax %2, %0, [%1]"
                   : "=r"(old)
                   : "r"(mem), "r"(val)
                   : "memory");
  return old;
}
__attribute__((noinline)) uint64_t do_ldumin(uint64_t* mem, uint64_t val) {
  uint64_t old;
  __asm__ volatile("ldumin %2, %0, [%1]"
                   : "=r"(old)
                   : "r"(mem), "r"(val)
                   : "memory");
  return old;
}

// CASP (32-bit pair): expected in W0:W1, desired in W2:W3, address in X4; the old
// pair is returned in W0:W1. Register-pinned because CASP requires consecutive
// even/odd register pairs.
__attribute__((noinline)) uint64_t do_casp32(uint32_t* mem, uint32_t exp_lo,
                                             uint32_t exp_hi, uint32_t new_lo,
                                             uint32_t new_hi) {
  register uint32_t s0 __asm__("w0") = exp_lo;
  register uint32_t s1 __asm__("w1") = exp_hi;
  register uint32_t t2 __asm__("w2") = new_lo;
  register uint32_t t3 __asm__("w3") = new_hi;
  register uint32_t* addr __asm__("x4") = mem;
  __asm__ volatile("casp w0, w1, w2, w3, [x4]"
                   : "+r"(s0), "+r"(s1)
                   : "r"(t2), "r"(t3), "r"(addr)
                   : "memory");
  return (static_cast<uint64_t>(s1) << 32) | s0;
}

int64_t s64(uint64_t x) { return static_cast<int64_t>(x); }

// Run one bitwise/min-max op kHotIters times, checking old + memory each time.
template <typename Op, typename Ref>
bool hot_check(Op op, Ref ref) {
  for (int i = 0; i < kHotIters; i++) {
    const uint64_t start = 0x00FF00FF00FF00FFULL ^ (static_cast<uint64_t>(i) << 8);
    const uint64_t val = 0xF0F0F0F0F0F0F0F0ULL + i;
    uint64_t mem = start;
    const uint64_t old = op(&mem, val);
    if (old != start || mem != ref(start, val)) return false;
  }
  return true;
}

bool hot_check_casp() {
  for (int i = 0; i < kHotIters; i++) {
    const uint32_t e_lo = 0x11111111u + i, e_hi = 0x22222222u + i;
    const uint32_t n_lo = 0xAAAAAAAAu, n_hi = 0xBBBBBBBBu;
    // Match case: memory holds the expected pair -> swap happens, old == expected.
    alignas(8) uint32_t mem[2] = {e_lo, e_hi};
    uint64_t old = do_casp32(mem, e_lo, e_hi, n_lo, n_hi);
    if (old != ((static_cast<uint64_t>(e_hi) << 32) | e_lo)) return false;
    if (mem[0] != n_lo || mem[1] != n_hi) return false;
    // Mismatch case: memory differs -> no swap, old == actual memory.
    alignas(8) uint32_t mem2[2] = {0xDEADBEEFu, 0xCAFEBABEu};
    uint64_t old2 = do_casp32(mem2, e_lo, e_hi, n_lo, n_hi);
    if (old2 != ((static_cast<uint64_t>(0xCAFEBABEu) << 32) | 0xDEADBEEFu)) return false;
    if (mem2[0] != 0xDEADBEEFu || mem2[1] != 0xCAFEBABEu) return false;
  }
  return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellolseatomics_MainActivity_probeLseatomics(JNIEnv* env,
                                                              jobject /*this*/) {
  std::string report = "ARMv8.1 LSE atomic RMW probe (heavy-tier):\n";
  char buf[128];

  struct Case { const char* name; bool ok; };
  const Case cases[] = {
      {"LDSET", hot_check(do_ldset, [](uint64_t a, uint64_t b) { return a | b; })},
      {"LDCLR", hot_check(do_ldclr, [](uint64_t a, uint64_t b) { return a & ~b; })},
      {"LDEOR", hot_check(do_ldeor, [](uint64_t a, uint64_t b) { return a ^ b; })},
      {"LDSMAX", hot_check(do_ldsmax, [](uint64_t a, uint64_t b) {
         return s64(a) > s64(b) ? a : b; })},
      {"LDSMIN", hot_check(do_ldsmin, [](uint64_t a, uint64_t b) {
         return s64(a) < s64(b) ? a : b; })},
      {"LDUMAX", hot_check(do_ldumax, [](uint64_t a, uint64_t b) {
         return a > b ? a : b; })},
      {"LDUMIN", hot_check(do_ldumin, [](uint64_t a, uint64_t b) {
         return a < b ? a : b; })},
      {"CASP", hot_check_casp()},
  };

  bool all_ok = true;
  for (const auto& c : cases) {
    snprintf(buf, sizeof(buf), "  %-7s x%d: %s\n", c.name, kHotIters,
             c.ok ? "OK" : "FAIL");
    report += buf;
    all_ok = all_ok && c.ok;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  if (!all_ok) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "LSE atomic mismatch under translation; aborting");
    abort();
  }
  return env->NewStringUTF(report.c_str());
}
