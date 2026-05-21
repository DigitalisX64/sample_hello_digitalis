// hello-bti: integration-level probe for Armv8.5-BTI (§J1 / §M1).
//
// BTI (Branch Target Identification) instructions are HINT-space NOPs on
// any CPU that does not implement BTI.  Digitalis does not enforce BTI;
// the only requirement is that the decoder route every HINT in the
// 0x20-0x27 block to its Nop() callback, never to Undefined().  If the
// routing is wrong the probe SIGILLs on the first BTI execution.
//
// Probe coverage:
//   1) Four inline-asm probes — one per BTI mnemonic.  Each executes
//      the BTI as a straight-line instruction (on hardware without BTI
//      a guard is just a NOP, on hardware with BTI it's a NOP unless
//      preceded by an indirect branch).
//   2) An indirect-call probe — a function pointer is dispatched
//      through BLR, landing on a function whose first instruction is
//      `bti c` (auto-emitted by clang under -mbranch-protection=bti).
//      A misrouted BTI c would SIGILL here.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellobti"

namespace {

inline bool probe_bti() {
  __asm__ __volatile__("bti" ::: "memory");
  return true;
}
inline bool probe_bti_c() {
  __asm__ __volatile__("bti c" ::: "memory");
  return true;
}
inline bool probe_bti_j() {
  __asm__ __volatile__("bti j" ::: "memory");
  return true;
}
inline bool probe_bti_jc() {
  __asm__ __volatile__("bti jc" ::: "memory");
  return true;
}

// Indirect-call target.  Under -mbranch-protection=bti the compiler
// emits a `bti c` as the first instruction (the address is taken so the
// linker can't prove indirect-call won't reach it).
__attribute__((noinline))
int indirect_target(int x) {
  __asm__ __volatile__("" ::: "memory");
  return x * 2 + 1;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellobti_MainActivity_probeBti(JNIEnv* env,
                                                jobject /*this*/) {
  std::string report = "Armv8.5-BTI probe:\n";
  char buf[160];
  int total = 0, passed = 0;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Armv8.5-BTI probe:");

  struct Row {
    const char* name;
    bool (*fn)();
  };
  const Row inline_probes[] = {
      {"bti",    probe_bti},
      {"bti c",  probe_bti_c},
      {"bti j",  probe_bti_j},
      {"bti jc", probe_bti_jc},
  };
  for (const auto& r : inline_probes) {
    bool ok = r.fn();
    ++total;
    if (ok) ++passed;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "  %-10s -> NOP survived: %s", r.name,
                        ok ? "OK" : "FAIL");
    snprintf(buf, sizeof(buf),
             "  %-10s -> NOP survived: %s\n", r.name,
             ok ? "OK" : "FAIL");
    report += buf;
  }

  // Indirect call through a function pointer.  Under
  // -mbranch-protection=bti the call site emits BLR <reg>, and the
  // target function's prologue begins with `bti c`.  Volatile forbids
  // the compiler from devirtualizing it.
  int (* volatile fp)(int) = &indirect_target;
  int got = fp(7);
  bool ind_ok = (got == 15);
  ++total;
  if (ind_ok) ++passed;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "  indir-call -> got=%d, expect=15: %s",
                      got, ind_ok ? "OK" : "FAIL");
  snprintf(buf, sizeof(buf),
           "  %-10s -> got=%d, expect=15: %s\n",
           "indir-call", got, ind_ok ? "OK" : "FAIL");
  report += buf;

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Summary: %d/%d OK",
                      passed, total);
  return env->NewStringUTF(report.c_str());
}
