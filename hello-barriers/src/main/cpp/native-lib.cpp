// hello-barriers: integration-level probes for ARM64 memory and synchronisation
// barriers.
//
// Locks in the §G2 audit: every barrier-class HINT and the
// CRn=0011 barrier family must decode to Nop() on Digitalis (we run above the
// kernel and inherit host memory ordering via x86-TSO; the locked atomics
// already provide release/acquire ordering ARM needs).  This sample emits
// one of each barrier mnemonic via inline asm; the probe is "did we survive
// past the barrier without an Undefined arm64 instruction SIGILL?"
//
// Probe coverage (per §G2):
//   YIELD                       -- HINT #1
//   WFE                         -- HINT #2
//   WFI                         -- HINT #3 (user-space NOP; kernel handles
//                                  real sleep)
//   SEV                         -- HINT #4
//   SEVL                        -- HINT #5
//   DMB  ish / ishld / ishst    -- data memory barrier, three common scopes
//   DSB  ish                    -- data sync barrier
//   ISB                         -- instruction sync barrier
//   CLREX                       -- clear exclusive monitor
//   SB                          -- Armv8.5 speculation barrier
//
// Each probe runs the barrier instruction in a loop with a memory-clobbered
// counter on both sides so the compiler can't elide the barrier-block as a
// dead store.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellobarriers"

namespace {

// Run `body` 16 times with a memory-clobbered counter on both sides.  The
// caller's `body` is a macro that emits a single barrier inline asm.  We use
// the counter as the OK signal: if the barrier raised SIGILL the process
// would die long before the post-loop check.
#define PROBE_BARRIER(name, asm_str)                              \
  inline bool probe_##name() {                                    \
    volatile uint32_t counter = 0;                                \
    for (int i = 0; i < 16; ++i) {                                \
      counter++;                                                  \
      __asm__ __volatile__(asm_str ::: "memory");                 \
      counter++;                                                  \
    }                                                             \
    return counter == 32;                                         \
  }

PROBE_BARRIER(yield, "yield")
PROBE_BARRIER(wfe, "wfe")
// WFI in EL0 is documented as a NOP-or-trap depending on system control.
// Bionic uses it via __builtin_arm_wfi; Digitalis runs above the host kernel
// so the architectural intent (sleep) is irrelevant -- what matters is the
// instruction doesn't crash.
PROBE_BARRIER(wfi, "wfi")
PROBE_BARRIER(sev, "sev")
PROBE_BARRIER(sevl, "sevl")

PROBE_BARRIER(dmb_ish, "dmb ish")
PROBE_BARRIER(dmb_ishld, "dmb ishld")
PROBE_BARRIER(dmb_ishst, "dmb ishst")
PROBE_BARRIER(dmb_sy, "dmb sy")

PROBE_BARRIER(dsb_ish, "dsb ish")
PROBE_BARRIER(dsb_sy, "dsb sy")
PROBE_BARRIER(dsb_oshld, "dsb oshld")

PROBE_BARRIER(isb, "isb")

PROBE_BARRIER(clrex, "clrex")

// SB (Armv8.5 speculation barrier): encoding d50330ff.  Assembled directly
// via .inst so this builds even on toolchains where the mnemonic isn't
// recognised; the -march=armv8.5-a flag on CMakeLists.txt allows the
// mnemonic too, but .inst is belt-and-braces.
inline bool probe_sb() {
  volatile uint32_t counter = 0;
  for (int i = 0; i < 16; ++i) {
    counter++;
    __asm__ __volatile__(".inst 0xd50330ff" ::: "memory");
    counter++;
  }
  return counter == 32;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellobarriers_MainActivity_probeBarriers(JNIEnv* env,
                                                          jobject /*this*/) {
  std::string report = "Armv8 barrier / hint probe:\n";
  char buf[128];

  struct Probe {
    const char* name;
    bool (*fn)();
  };
  const Probe probes[] = {
      {"YIELD", probe_yield},
      {"WFE", probe_wfe},
      {"WFI", probe_wfi},
      {"SEV", probe_sev},
      {"SEVL", probe_sevl},
      {"DMB ISH", probe_dmb_ish},
      {"DMB ISHLD", probe_dmb_ishld},
      {"DMB ISHST", probe_dmb_ishst},
      {"DMB SY", probe_dmb_sy},
      {"DSB ISH", probe_dsb_ish},
      {"DSB SY", probe_dsb_sy},
      {"DSB OSHLD", probe_dsb_oshld},
      {"ISB", probe_isb},
      {"CLREX", probe_clrex},
      {"SB", probe_sb},
  };

  int total = 0;
  int passed = 0;
  for (const auto& p : probes) {
    bool ok = p.fn();
    snprintf(buf, sizeof(buf), "  %-10s: %s\n", p.name, ok ? "OK" : "FAIL");
    report += buf;
    total++;
    if (ok) passed++;
  }

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
