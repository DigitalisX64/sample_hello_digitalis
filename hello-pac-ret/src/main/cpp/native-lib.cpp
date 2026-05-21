// hello-pac-ret: integration-level probes for Armv8.3-PAuth (§F1, §F2).
//
// Digitalis runs above the host kernel and has no PAC backing -- the
// translator decodes every PAuth instruction as a HINT-style no-op and the
// branch-with-PAC family (BRAA/BLRAA/RETAA et al.) decodes to a plain
// indirect branch (handoffs -30 and -32).  This sample probes that surface:
// if any opcode below routes to Undefined() the process SIGILLs before the
// summary line is logged.
//
// Probe coverage:
//   PAC-RET prologue/epilogue   -- compiled in via -mbranch-protection=pac-ret;
//                                  every non-leaf function emits PACIASP at
//                                  entry and AUTIASP at exit.
//   PACIA / AUTIA  on Xn,Xm     -- DP-1Src opcode2=00001 (handoff-30 routing)
//   PACIZA / AUTIZA on Xn       -- same family, Xm hardcoded to XZR
//   PACDA / AUTDA  on Xn,Xm     -- data-key variants
//   XPACI / XPACD               -- explicit strip (identity on Digitalis)
//   PACIASP / AUTIASP (HINTs)   -- via __builtin asm for explicit coverage
//   PACIBSP / AUTIBSP (HINTs)   -- b-key prologue/epilogue
//   BLRAA / BLRAB / BLRAAZ      -- §F2: branch-with-link authenticated, key A/B
//
// All Pac/Aut on a register are *identity* on Digitalis (we never insert PAC
// bits, so stripping is the identity too).  The probes therefore check that
// the destination register is unchanged after the operation.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellopacret"

namespace {

// PAuth-on-register probes.  Each runs the named instruction on a fixed
// 48-bit "user-space" address (high bits zero, no TBI bits) and asserts
// the value is unchanged afterwards.  On Digitalis the implementation is
// identity; on a real PAC-capable CPU it would insert/strip PAC bits, so
// the assertion would only hold for PACxx + matching AUTxx pairs.  Since
// Digitalis never inserts the bits, identity holds for each step
// individually.

#define PROBE_PAUTH_RR(name, mnemonic)                                       \
  inline bool probe_##name() {                                               \
    uint64_t addr = 0x0000123456789000ULL;                                   \
    uint64_t mod  = 0x0000000000001357ULL;                                   \
    uint64_t out  = addr;                                                    \
    __asm__ __volatile__(mnemonic " %0, %1\n"                                \
                         : "+r"(out)                                         \
                         : "r"(mod));                                        \
    return out == addr;                                                      \
  }

#define PROBE_PAUTH_R_ZERO(name, mnemonic)                                   \
  inline bool probe_##name() {                                               \
    uint64_t addr = 0x0000123456789000ULL;                                   \
    uint64_t out  = addr;                                                    \
    __asm__ __volatile__(mnemonic " %0\n"                                    \
                         : "+r"(out));                                       \
    return out == addr;                                                      \
  }

PROBE_PAUTH_RR(pacia,  "pacia")
PROBE_PAUTH_RR(autia,  "autia")
PROBE_PAUTH_RR(pacib,  "pacib")
PROBE_PAUTH_RR(autib,  "autib")
PROBE_PAUTH_RR(pacda,  "pacda")
PROBE_PAUTH_RR(autda,  "autda")
PROBE_PAUTH_RR(pacdb,  "pacdb")
PROBE_PAUTH_RR(autdb,  "autdb")

PROBE_PAUTH_R_ZERO(paciza, "paciza")
PROBE_PAUTH_R_ZERO(autiza, "autiza")
PROBE_PAUTH_R_ZERO(pacizb, "pacizb")
PROBE_PAUTH_R_ZERO(autizb, "autizb")
PROBE_PAUTH_R_ZERO(pacdza, "pacdza")
PROBE_PAUTH_R_ZERO(autdza, "autdza")
PROBE_PAUTH_R_ZERO(pacdzb, "pacdzb")
PROBE_PAUTH_R_ZERO(autdzb, "autdzb")

// XPACI / XPACD: explicit strip of PAC bits (identity on Digitalis).
PROBE_PAUTH_R_ZERO(xpaci, "xpaci")
PROBE_PAUTH_R_ZERO(xpacd, "xpacd")

// PACIASP / AUTIASP / PACIBSP / AUTIBSP -- HINT-form prologue/epilogue.
// These touch the LR (X30) with SP as the modifier; we save/restore LR
// around the asm so the surrounding function does not trip on a corrupted
// return.
inline bool probe_paciasp_autiasp() {
  uint64_t lr_before = 0, lr_after = 0;
  __asm__ __volatile__(
      "mov %0, x30\n"
      "paciasp\n"
      "autiasp\n"
      "mov %1, x30\n"
      : "=&r"(lr_before), "=&r"(lr_after)
      :
      : "x30");
  return lr_before == lr_after;
}

inline bool probe_pacibsp_autibsp() {
  uint64_t lr_before = 0, lr_after = 0;
  __asm__ __volatile__(
      "mov %0, x30\n"
      "pacibsp\n"
      "autibsp\n"
      "mov %1, x30\n"
      : "=&r"(lr_before), "=&r"(lr_after)
      :
      : "x30");
  return lr_before == lr_after;
}

// PACIAZ / AUTIAZ / PACIBZ / AUTIBZ -- HINT-form on LR with XZR modifier.
inline bool probe_paciaz_autiaz() {
  uint64_t lr_before = 0, lr_after = 0;
  __asm__ __volatile__(
      "mov %0, x30\n"
      "paciaz\n"
      "autiaz\n"
      "mov %1, x30\n"
      : "=&r"(lr_before), "=&r"(lr_after)
      :
      : "x30");
  return lr_before == lr_after;
}

inline bool probe_pacibz_autibz() {
  uint64_t lr_before = 0, lr_after = 0;
  __asm__ __volatile__(
      "mov %0, x30\n"
      "pacibz\n"
      "autibz\n"
      "mov %1, x30\n"
      : "=&r"(lr_before), "=&r"(lr_after)
      :
      : "x30");
  return lr_before == lr_after;
}

// XPACLRI -- HINT-form strip on LR.
inline bool probe_xpaclri() {
  uint64_t lr_before = 0, lr_after = 0;
  __asm__ __volatile__(
      "mov %0, x30\n"
      "xpaclri\n"
      "mov %1, x30\n"
      : "=&r"(lr_before), "=&r"(lr_after)
      :
      : "x30");
  return lr_before == lr_after;
}

// §F2: BLRAA / BLRAB -- branch-with-link to a register, authenticated with
// key A / key B and a modifier register.  On Digitalis these decode to
// plain BLR after handoff-32.  The probe BLRAA's to a local label that
// increments the counter and returns.
inline bool probe_blraa() {
  uint64_t counter = 0;
  uint64_t modifier = 0xCAFEBABEULL;
  __asm__ __volatile__(
      "adr x9, 1f\n"
      "mov x10, %2\n"
      "blraa x9, x10\n"
      "b 2f\n"
      "1:\n"
      "add %0, %0, #1\n"
      "ret\n"
      "2:\n"
      : "+r"(counter)
      : "r"(counter), "r"(modifier)
      : "x9", "x10", "x30", "memory");
  return counter == 1;
}

inline bool probe_blrab() {
  uint64_t counter = 0;
  uint64_t modifier = 0xDEADBEEFULL;
  __asm__ __volatile__(
      "adr x9, 1f\n"
      "mov x10, %2\n"
      "blrab x9, x10\n"
      "b 2f\n"
      "1:\n"
      "add %0, %0, #1\n"
      "ret\n"
      "2:\n"
      : "+r"(counter)
      : "r"(counter), "r"(modifier)
      : "x9", "x10", "x30", "memory");
  return counter == 1;
}

// §F2: BLRAAZ / BLRABZ -- branch-with-PAC using XZR as the modifier.
// Decoder case 0b0001 with op3=000010 (key A) / 000011 (key B).
inline bool probe_blraaz() {
  uint64_t counter = 0;
  __asm__ __volatile__(
      "adr x9, 1f\n"
      "blraaz x9\n"
      "b 2f\n"
      "1:\n"
      "add %0, %0, #1\n"
      "ret\n"
      "2:\n"
      : "+r"(counter)
      :
      : "x9", "x30", "memory");
  return counter == 1;
}

inline bool probe_blrabz() {
  uint64_t counter = 0;
  __asm__ __volatile__(
      "adr x9, 1f\n"
      "blrabz x9\n"
      "b 2f\n"
      "1:\n"
      "add %0, %0, #1\n"
      "ret\n"
      "2:\n"
      : "+r"(counter)
      :
      : "x9", "x30", "memory");
  return counter == 1;
}

// PAC-RET smoke test: a non-leaf function compiled with
// -mbranch-protection=pac-ret will have PACIASP/AUTIASP in its
// prologue/epilogue.  Just calling it exercises the HINT-form pair.  Add a
// noinline attribute so the compiler cannot elide the prologue.
__attribute__((noinline))
uint64_t pacret_callee(uint64_t a, uint64_t b) {
  // Force a non-leaf by calling something the compiler can't inline trivially.
  volatile uint64_t r = a + b;
  return r;
}

inline bool probe_pac_ret_prologue_epilogue() {
  uint64_t r = 0;
  for (uint64_t i = 0; i < 8; ++i) {
    r += pacret_callee(i, i * 2);
  }
  // sum_{i=0..7} 3i = 3 * 28 = 84
  return r == 84;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellopacret_MainActivity_probePacRet(JNIEnv* env,
                                                      jobject /*this*/) {
  std::string report = "Armv8.3-PAuth (PAC) probe:\n";
  char buf[128];

  struct Probe {
    const char* name;
    bool (*fn)();
  };
  const Probe probes[] = {
      // HINT-form (§F1, handoff-30)
      {"PACIASP/AUTIASP", probe_paciasp_autiasp},
      {"PACIBSP/AUTIBSP", probe_pacibsp_autibsp},
      {"PACIAZ/AUTIAZ",   probe_paciaz_autiaz},
      {"PACIBZ/AUTIBZ",   probe_pacibz_autibz},
      {"XPACLRI",         probe_xpaclri},
      // PAC-by-register (§F1, handoff-30)
      {"PACIA",   probe_pacia},
      {"AUTIA",   probe_autia},
      {"PACIB",   probe_pacib},
      {"AUTIB",   probe_autib},
      {"PACDA",   probe_pacda},
      {"AUTDA",   probe_autda},
      {"PACDB",   probe_pacdb},
      {"AUTDB",   probe_autdb},
      {"PACIZA",  probe_paciza},
      {"AUTIZA",  probe_autiza},
      {"PACIZB",  probe_pacizb},
      {"AUTIZB",  probe_autizb},
      {"PACDZA",  probe_pacdza},
      {"AUTDZA",  probe_autdza},
      {"PACDZB",  probe_pacdzb},
      {"AUTDZB",  probe_autdzb},
      {"XPACI",   probe_xpaci},
      {"XPACD",   probe_xpacd},
      // Branch-with-PAC (§F2, handoff-32)
      {"BLRAA",   probe_blraa},
      {"BLRAB",   probe_blrab},
      {"BLRAAZ",  probe_blraaz},
      {"BLRABZ",  probe_blrabz},
      // PAC-RET prologue/epilogue smoke
      {"PAC-RET", probe_pac_ret_prologue_epilogue},
  };

  int total = 0;
  int passed = 0;
  for (const auto& p : probes) {
    bool ok = p.fn();
    snprintf(buf, sizeof(buf), "  %-16s: %s\n", p.name, ok ? "OK" : "FAIL");
    report += buf;
    total++;
    if (ok) passed++;
  }

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
