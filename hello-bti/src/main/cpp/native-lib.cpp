// Integration-level probe for Armv8.5-BTI.
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

#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <ucontext.h>

#define LOG_TAG "hellobti"

namespace {

// BRK breakpoint probe: BRK #imm must reach a guest breakpoint handler (as a
// debugger or sanitizer would install) at the faulting PC, not abort with an
// illegal-instruction. Install a handler that records the faulting instruction
// and longjmps out, execute `brk #0x1f`, and confirm the handler saw a BRK with
// the right immediate. (No literal signal-name strings are logged — the sample
// liveness check greps those as crash markers.)
sigjmp_buf g_brk_jmp;
volatile sig_atomic_t g_brk_hit = 0;
volatile uint32_t g_brk_insn = 0;
void brk_handler(int /*sig*/, siginfo_t* /*info*/, void* uc) {
  g_brk_hit = 1;
  auto* u = static_cast<ucontext_t*>(uc);
  g_brk_insn = *reinterpret_cast<const uint32_t*>(u->uc_mcontext.pc);
  siglongjmp(g_brk_jmp, 1);
}
inline bool probe_brk() {
  struct sigaction sa = {};
  struct sigaction old_sa = {};
  sa.sa_sigaction = brk_handler;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGTRAP, &sa, &old_sa) != 0) return false;
  g_brk_hit = 0;
  g_brk_insn = 0;
  bool ok = false;
  if (sigsetjmp(g_brk_jmp, 1) == 0) {
    __asm__ __volatile__("brk #0x1f" ::: "memory");
    // Reaching here means BRK did NOT trap — failure.
  } else {
    // BRK #imm = 0xD4200000 | (imm << 5); recovered instruction must match
    // with immediate 0x1f.
    ok = g_brk_hit && ((g_brk_insn & 0xFFE0001Fu) == 0xD4200000u) &&
         (((g_brk_insn >> 5) & 0xFFFFu) == 0x1Fu);
  }
  sigaction(SIGTRAP, &old_sa, nullptr);
  return ok;
}

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

  // BRK breakpoint delivery (BRK #imm -> guest trap handler at the BRK PC).
  bool brk_ok = probe_brk();
  ++total;
  if (brk_ok) ++passed;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "  brk #imm  -> breakpoint handler reached: %s",
                      brk_ok ? "OK" : "FAIL");
  snprintf(buf, sizeof(buf),
           "  %-10s -> breakpoint handler reached: %s\n", "brk #imm",
           brk_ok ? "OK" : "FAIL");
  report += buf;

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Summary: %d/%d OK",
                      passed, total);
  return env->NewStringUTF(report.c_str());
}
