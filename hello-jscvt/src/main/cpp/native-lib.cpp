// hello-jscvt: integration-level probe for Armv8.3-JSCVT (§D4 / §M1).
//
// FJCVTZS converts a double-precision float to a 32-bit signed integer
// using ECMAScript ToInt32 semantics: round toward zero with modular
// reduction (NOT saturation).  PSTATE.Z is set iff the conversion was
// exact (no rounding, no saturation, not NaN/Inf); N/C/V are always 0.
// The instruction is unconditional under -march=armv8.3-a+jscvt.
//
// Probe coverage:
//   + and - exact integers in range            (Z=1 expected)
//   + and - fractional values in range         (truncated, Z=0)
//   INT32_MAX / INT32_MIN exact                (Z=1)
//   2^31 (just above INT32_MAX)                (wraps to INT32_MIN, Z=0)
//   -2^31 - 1 (just below INT32_MIN)           (wraps to INT32_MAX, Z=0)
//   2^32                                       (modular -> 0, Z=0)
//   5e9 / -5e9                                 (modular reduce, Z=0)
//   NaN / +Inf / -Inf                          (-> 0, Z=0)
//
// If Digitalis routes FJCVTZS to Undefined() the process SIGILLs on the
// first probe; if the modular-reduction or Z-flag logic is wrong the
// FAIL rows pinpoint the broken case.

#include <android/log.h>
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#define LOG_TAG "hellojscvt"

namespace {

struct FjcvtRes {
  int32_t value;
  bool z_flag;  // PSTATE.Z bit, set when conversion was exact
};

// Issue FJCVTZS Wd, Dn and immediately MRS the resulting NZCV into a GP
// register so we can observe the exactness flag.  Clang accepts the
// `fjcvtzs` mnemonic when the file is compiled with -march=armv8.3-a+jscvt
// (see CMakeLists.txt).
inline FjcvtRes run_fjcvtzs(double d) {
  uint32_t out;
  uint64_t nzcv;
  __asm__ __volatile__(
      "fjcvtzs %w0, %d2\n"
      "mrs %1, nzcv\n"
      : "=r"(out), "=r"(nzcv)
      : "w"(d)
      : "cc");
  // NZCV system register layout: bits[31:28] = {N, Z, C, V}.  Z is bit 30.
  return {static_cast<int32_t>(out), (nzcv & (1ull << 30)) != 0};
}

struct Probe {
  const char* name;
  double input;
  int32_t expected_value;
  bool expected_exact;
};

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellojscvt_MainActivity_probeJscvt(JNIEnv* env,
                                                    jobject /*this*/) {
  std::string report = "Armv8.3-JSCVT (FJCVTZS) probe:\n";
  char buf[192];

  const double kInf = std::numeric_limits<double>::infinity();

  const Probe probes[] = {
      // Zero.
      {"+0.0",         0.0,            0,          true},
      // Positive in-range.
      {"+42.0",        42.0,           42,         true},
      {"+42.5",        42.5,           42,         false},
      {"+42.999",      42.999,         42,         false},
      // Negative in-range (trunc toward zero, not floor).
      {"-42.0",        -42.0,          -42,        true},
      {"-42.5",        -42.5,          -42,        false},
      // INT32 boundary, exact -> Z=1.
      {"INT32_MAX",    2147483647.0,   INT32_MAX,  true},
      {"INT32_MIN",    -2147483648.0,  INT32_MIN,  true},
      // Just above INT32_MAX: ToInt32(2^31) = INT32_MIN.
      {"+2^31",        2147483648.0,   INT32_MIN,  false},
      // Below INT32_MIN by 1: ToInt32(-2^31 - 1) = INT32_MAX.
      {"-2^31 - 1",    -2147483649.0,  INT32_MAX,  false},
      // 2^32 reduces to 0.
      {"+2^32",        4294967296.0,   0,          false},
      // 5e9 mod 2^32 = 705032704.
      {"+5e9",         5000000000.0,   705032704,  false},
      // -5e9 ECMAScript ToInt32 = -705032704.
      {"-5e9",         -5000000000.0,  -705032704, false},
      // NaN / Inf -> 0.
      {"NaN",          std::nan(""),   0,          false},
      {"+Inf",         kInf,           0,          false},
      {"-Inf",         -kInf,          0,          false},
  };

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Armv8.3-JSCVT (FJCVTZS) probe:");

  int total = 0, passed = 0;
  for (const auto& p : probes) {
    FjcvtRes r = run_fjcvtzs(p.input);
    bool ok = (r.value == p.expected_value) && (r.z_flag == p.expected_exact);
    // Log per-line (no trailing newline) because __android_log_print has a
    // 1024-byte internal buffer and the full report (16 probes + summary)
    // is just over that.
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "  %-12s -> %11d (Z=%d), expect %11d (Z=%d): %s",
                        p.name, r.value, r.z_flag ? 1 : 0,
                        p.expected_value, p.expected_exact ? 1 : 0,
                        ok ? "OK" : "FAIL");
    // Accumulate the on-screen report (TextView shows this).
    snprintf(buf, sizeof(buf),
             "  %-12s -> %11d (Z=%d), expect %11d (Z=%d): %s\n",
             p.name, r.value, r.z_flag ? 1 : 0,
             p.expected_value, p.expected_exact ? 1 : 0,
             ok ? "OK" : "FAIL");
    report += buf;
    ++total;
    if (ok) ++passed;
  }

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Summary: %d/%d OK",
                      passed, total);
  return env->NewStringUTF(report.c_str());
}
