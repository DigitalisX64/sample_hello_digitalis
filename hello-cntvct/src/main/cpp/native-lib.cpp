// Integration-level probes for the ARMv8 generic-timer system registers read
// via MRS: CNTFRQ_EL0, CNTVCT_EL0, CNTPCT_EL0.
//
// Timing and benchmark code (game engines, profilers, std::chrono backends)
// reads these for a cheap monotonic clock. They are EL0-readable and must not
// fault; a decoder/interpreter that lacked them raised "Undefined arm64
// instruction". Each value is read with an inline-asm MRS so the compiler
// can't fold it to a library call.
//
// The checks avoid pinning an exact frequency (real hardware varies): CNTFRQ
// must be non-zero, and the virtual/physical counters must be monotonically
// non-decreasing across reads separated by a busy wait.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellocntvct"

namespace {

inline uint64_t read_cntfrq() {
  uint64_t v;
  __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(v));
  return v;
}

inline uint64_t read_cntvct() {
  uint64_t v;
  __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
  return v;
}

inline uint64_t read_cntpct() {
  uint64_t v;
  __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(v));
  return v;
}

// A volatile busy wait so the counters have a chance to advance between reads
// without the loop being optimised away.
inline void busy_wait() {
  volatile uint64_t spin = 0;
  for (uint64_t i = 0; i < 2'000'000ULL; ++i) spin += i;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellocntvct_MainActivity_probeCntvct(JNIEnv* env,
                                                      jobject /*this*/) {
  std::string report = "ARMv8 generic-timer MRS probe:\n";
  char buf[256];

  const uint64_t frq = read_cntfrq();
  bool frq_ok = (frq != 0);
  snprintf(buf, sizeof(buf), "  CNTFRQ_EL0: %llu Hz -> %s\n",
           static_cast<unsigned long long>(frq), frq_ok ? "OK" : "FAIL");
  report += buf;

  const uint64_t vct0 = read_cntvct();
  busy_wait();
  const uint64_t vct1 = read_cntvct();
  bool vct_ok = (vct0 != 0) && (vct1 >= vct0);
  snprintf(buf, sizeof(buf), "  CNTVCT_EL0: %llu -> %llu (%s)\n",
           static_cast<unsigned long long>(vct0),
           static_cast<unsigned long long>(vct1), vct_ok ? "OK" : "FAIL");
  report += buf;

  const uint64_t pct0 = read_cntpct();
  busy_wait();
  const uint64_t pct1 = read_cntpct();
  bool pct_ok = (pct0 != 0) && (pct1 >= pct0);
  snprintf(buf, sizeof(buf), "  CNTPCT_EL0: %llu -> %llu (%s)\n",
           static_cast<unsigned long long>(pct0),
           static_cast<unsigned long long>(pct1), pct_ok ? "OK" : "FAIL");
  report += buf;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
