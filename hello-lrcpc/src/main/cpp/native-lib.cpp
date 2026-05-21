// hello-lrcpc: integration-level probes for Armv8.3-LRCPC and Armv8.1-LOR
// (plan §G1).
//
// Digitalis runs above an x86-TSO host whose memory model is strictly
// stronger than Arm's RCpc (load-acquire RCpc Register) and stronger than
// the limited-ordering region store-release.  The translator therefore
// decodes LDAPR/LDAPRB/LDAPRH and LDLAR/STLLR/STLLRB/STLLRH as plain x86
// loads/stores (the latter routed to kLdar/kStlr in handoff-31; the
// former gated on Rs=11111 in DecodeAtomicMemoryOp).
//
// This sample exercises each opcode via inline asm against a stack buffer.
// All probes are single-threaded; they only verify that the instruction
// decodes, executes, and returns the value last stored at the same
// address.  Any decoder routing regression (Undefined()) would SIGILL the
// process before the summary line is logged.
//
// Probe coverage:
//   LDAPR  (X form)  -- 64-bit acquire load (RCpc)
//   LDAPR  (W form)  -- 32-bit acquire load (RCpc)
//   LDAPRB           -- 8-bit acquire load (RCpc)
//   LDAPRH           -- 16-bit acquire load (RCpc)
//   LDLAR  (X form)  -- 64-bit limited-ordering acquire load (LOR)
//   LDLAR  (W form)  -- 32-bit
//   LDLARB           -- 8-bit
//   LDLARH           -- 16-bit
//   STLLR  (X form)  -- 64-bit limited-ordering release store (LOR)
//   STLLR  (W form)  -- 32-bit
//   STLLRB           -- 8-bit
//   STLLRH           -- 16-bit
//
// Implementation notes for the inline-asm macros:
//   * The address operand uses an early-clobber input ("r"(&buf)) so the
//     compiler can park it in any GPR; we never assume a fixed register.
//   * For the load probes the buffer holds a non-zero canary written by
//     plain C++; if the load read the wrong address or the decoder swapped
//     the destination register the canary would not appear.
//   * For the store probes we zero the buffer first, do the STLLR, then
//     read it back via plain memcpy and compare.  This means the verify
//     side is C++ and never touches LDAPR/LDLAR, so a STLLR failure does
//     not get masked by a simultaneous load-side regression.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstring>
#include <string>

#define LOG_TAG "hellolrcpc"

namespace {

// ---- LDAPR -----------------------------------------------------------

inline bool probe_ldapr_x() {
  volatile uint64_t buf = 0xCAFEF00DDEADBEEFULL;
  uint64_t out = 0;
  __asm__ __volatile__("ldapr %0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return out == 0xCAFEF00DDEADBEEFULL;
}

inline bool probe_ldapr_w() {
  volatile uint32_t buf = 0xDEADBEEFu;
  uint32_t out = 0;
  __asm__ __volatile__("ldapr %w0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return out == 0xDEADBEEFu;
}

inline bool probe_ldaprb() {
  volatile uint8_t buf = 0xA5u;
  uint32_t out = 0;
  __asm__ __volatile__("ldaprb %w0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return (out & 0xFFu) == 0xA5u;
}

inline bool probe_ldaprh() {
  volatile uint16_t buf = 0xBEEFu;
  uint32_t out = 0;
  __asm__ __volatile__("ldaprh %w0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return (out & 0xFFFFu) == 0xBEEFu;
}

// ---- LDLAR (LOR-load) ------------------------------------------------

inline bool probe_ldlar_x() {
  volatile uint64_t buf = 0x0123456789ABCDEFULL;
  uint64_t out = 0;
  __asm__ __volatile__("ldlar %0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return out == 0x0123456789ABCDEFULL;
}

inline bool probe_ldlar_w() {
  volatile uint32_t buf = 0x12345678u;
  uint32_t out = 0;
  __asm__ __volatile__("ldlar %w0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return out == 0x12345678u;
}

inline bool probe_ldlarb() {
  volatile uint8_t buf = 0x5Au;
  uint32_t out = 0;
  __asm__ __volatile__("ldlarb %w0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return (out & 0xFFu) == 0x5Au;
}

inline bool probe_ldlarh() {
  volatile uint16_t buf = 0x1234u;
  uint32_t out = 0;
  __asm__ __volatile__("ldlarh %w0, [%1]"
                       : "=r"(out)
                       : "r"(&buf)
                       : "memory");
  return (out & 0xFFFFu) == 0x1234u;
}

// ---- STLLR (LOR-store) -----------------------------------------------

inline bool probe_stllr_x() {
  volatile uint64_t buf = 0;
  uint64_t val = 0xFEEDFACECAFED00DULL;
  __asm__ __volatile__("stllr %1, [%0]"
                       :
                       : "r"(&buf), "r"(val)
                       : "memory");
  return buf == 0xFEEDFACECAFED00DULL;
}

inline bool probe_stllr_w() {
  volatile uint32_t buf = 0;
  uint32_t val = 0xBADC0FFEu;
  __asm__ __volatile__("stllr %w1, [%0]"
                       :
                       : "r"(&buf), "r"(val)
                       : "memory");
  return buf == 0xBADC0FFEu;
}

inline bool probe_stllrb() {
  volatile uint8_t buf = 0;
  uint32_t val = 0xC3u;
  __asm__ __volatile__("stllrb %w1, [%0]"
                       :
                       : "r"(&buf), "r"(val)
                       : "memory");
  return buf == 0xC3u;
}

inline bool probe_stllrh() {
  volatile uint16_t buf = 0;
  uint32_t val = 0xABCDu;
  __asm__ __volatile__("stllrh %w1, [%0]"
                       :
                       : "r"(&buf), "r"(val)
                       : "memory");
  return buf == 0xABCDu;
}

// Probe table.
struct Probe {
  const char* name;
  bool (*fn)();
};

const Probe kProbes[] = {
    {"LDAPR  (X) ",   probe_ldapr_x},
    {"LDAPR  (W) ",   probe_ldapr_w},
    {"LDAPRB     ",   probe_ldaprb},
    {"LDAPRH     ",   probe_ldaprh},
    {"LDLAR  (X) ",   probe_ldlar_x},
    {"LDLAR  (W) ",   probe_ldlar_w},
    {"LDLARB     ",   probe_ldlarb},
    {"LDLARH     ",   probe_ldlarh},
    {"STLLR  (X) ",   probe_stllr_x},
    {"STLLR  (W) ",   probe_stllr_w},
    {"STLLRB     ",   probe_stllrb},
    {"STLLRH     ",   probe_stllrh},
};

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellolrcpc_MainActivity_probeLrcpc(JNIEnv* env, jobject /*this*/) {
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Armv8.3-LRCPC / Armv8.1-LOR probe:");
  std::string result;
  int ok = 0;
  for (const auto& p : kProbes) {
    bool pass = p.fn();
    if (pass) ++ok;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "  %s: %s",
                        p.name, pass ? "OK" : "FAIL");
    result.append(p.name);
    result.append(pass ? ": OK\n" : ": FAIL\n");
  }
  char tail[64];
  std::snprintf(tail, sizeof(tail), "Summary: %d/%zu OK",
                ok, sizeof(kProbes) / sizeof(kProbes[0]));
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", tail);
  result.append(tail);
  return env->NewStringUTF(result.c_str());
}
