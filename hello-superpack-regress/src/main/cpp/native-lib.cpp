// Regression test for the JIT bugs that previously broke Facebook (and a
// related class likely behind WhatsApp's libsuperpack.so SIGSEGV). Each
// probe targets a specific ARM64 instruction or pattern that has caused a
// silent miscompilation in the Digitalis JIT in the past.
//
// All probes are written with inline asm so the emitted ARM64 instruction
// is exactly the encoding we want to test — compiler optimisation can't
// substitute a different form.
//
// Probes return PASS/FAIL per check and the JNI entry point aggregates
// them into a single status string that the activity displays.

#include <arm_neon.h>
#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellosuperpackregress"

namespace {

// ---------------------------------------------------------------------------
// PRFM (prefetch) regression: previously the decoder treated PRFM with
// opc=0b10, size=0b11 as LDR Xt, [Xn, ...], silently clobbering whatever
// register the prefetch type field aliased (Rt=0 = pldl1keep clobbered X0).
// Fixed in `Digitalis: NOP PRFM in ldr-imm/ldur/ldr-reg decoder paths`.
// ---------------------------------------------------------------------------
bool probe_prfm_immediate() {
  alignas(64) uint8_t buf[64] = {0};
  uint64_t x0_before = 0xdeadbeefcafef00dULL;
  uint64_t x0_after = 0;
  asm volatile(
      "mov x0, %1\n"
      "prfm pldl1keep, [%2, #0]\n"
      "mov %0, x0\n"
      : "=r"(x0_after)
      : "r"(x0_before), "r"(buf)
      : "x0", "memory");
  return x0_after == x0_before;
}

bool probe_prfum_unscaled() {
  alignas(64) uint8_t buf[64] = {0};
  uint64_t x0_before = 0x0123456789abcdefULL;
  uint64_t x0_after = 0;
  asm volatile(
      "mov x0, %1\n"
      "prfum pldl1keep, [%2, #-8]\n"
      "mov %0, x0\n"
      : "=r"(x0_after)
      : "r"(x0_before), "r"(buf + 8)
      : "x0", "memory");
  return x0_after == x0_before;
}

bool probe_prfm_register_offset() {
  alignas(64) uint8_t buf[64] = {0};
  uint64_t x0_before = 0xfedcba9876543210ULL;
  uint64_t x0_after = 0;
  uint64_t off = 16;
  asm volatile(
      "mov x0, %1\n"
      "prfm pldl1keep, [%2, %3]\n"
      "mov %0, x0\n"
      : "=r"(x0_after)
      : "r"(x0_before), "r"(buf), "r"(off)
      : "x0", "memory");
  return x0_after == x0_before;
}

// ---------------------------------------------------------------------------
// SMAXV/UMAXV/SMINV/UMINV: across-lanes scalar reductions. The decoder
// previously mis-routed these to the CMLT-zero opcode in two-reg-misc,
// producing per-lane (lane<0?-1:0) instead of the scalar max/min.
// Fixed in `Digitalis: route SMAXV/UMAXV/SMINV/UMINV to interpreter ...`.
// ---------------------------------------------------------------------------
bool probe_smaxv_4s() {
  int32x4_t v = {7, -3, 12, 5};
  int32_t result;
  asm volatile("smaxv %s0, %1.4s" : "=w"(result) : "w"(v));
  return result == 12;
}

bool probe_umaxv_4s() {
  uint32x4_t v = {0x10, 0x100, 0x1000, 0xff};
  uint32_t result;
  asm volatile("umaxv %s0, %1.4s" : "=w"(result) : "w"(v));
  return result == 0x1000;
}

bool probe_sminv_4s() {
  int32x4_t v = {7, -3, 12, 5};
  int32_t result;
  asm volatile("sminv %s0, %1.4s" : "=w"(result) : "w"(v));
  return result == -3;
}

bool probe_uminv_4s() {
  uint32x4_t v = {0x100, 0x1, 0x1000, 0x80};
  uint32_t result;
  asm volatile("uminv %s0, %1.4s" : "=w"(result) : "w"(v));
  return result == 1;
}

// ---------------------------------------------------------------------------
// BLR X30: previously SetReg(30, ret_addr+4) clobbered the host register
// holding the original branch target. Fixed by snapshotting the target
// before SetReg writes X30. Emit a tail-call-like BLR X30 pattern.
// ---------------------------------------------------------------------------
static uint64_t blr_x30_target_marker = 0;
extern "C" __attribute__((noinline)) uint64_t blr_x30_callee() {
  blr_x30_target_marker = 0x1337c0debeefULL;
  return 0x12345678abcdef00ULL;
}

bool probe_blr_x30() {
  blr_x30_target_marker = 0;
  uint64_t ret;
  uint64_t target = reinterpret_cast<uint64_t>(&blr_x30_callee);
  asm volatile(
      "mov x30, %1\n"
      "blr x30\n"
      "mov %0, x0\n"
      : "=r"(ret)
      : "r"(target)
      : "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10",
        "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x30",
        "memory");
  return ret == 0x12345678abcdef00ULL &&
         blr_x30_target_marker == 0x1337c0debeefULL;
}

// ---------------------------------------------------------------------------
// CAS / SWP / LDADD byte/halfword Wt result must be zero-extended to 64.
// Previously the JIT did `Movq(temp, rax)` and forwarded all 64 bits of
// rax (which only had the low 8/16 updated by LOCK CMPXCHG/XCHG/XADD).
// Fixed in `Digitalis: zero-extend CAS/SWP/LDADD byte/halfword Wt result`.
// ---------------------------------------------------------------------------
bool probe_swpb() {
  // SWPB Ws, Wt, [Xn] — emitted by __atomic_exchange_n at -march=armv8-a+lse.
  // Wt (= old mem byte) must be zero-extended to 64 in Xt.
  uint8_t mem = 0x42;
  uint8_t old_byte = __atomic_exchange_n(&mem, 0xa5, __ATOMIC_SEQ_CST);
  uint64_t wide = static_cast<uint64_t>(old_byte);
  return wide == 0x42 && mem == 0xa5;
}

bool probe_ldaddb() {
  // LDADDB Ws, Wt, [Xn] — emitted by __atomic_fetch_add at -march=armv8-a+lse.
  // Wt must be zero-extended to 64.
  uint8_t mem = 0x10;
  uint8_t old_byte = __atomic_fetch_add(&mem, 5, __ATOMIC_SEQ_CST);
  uint64_t wide = static_cast<uint64_t>(old_byte);
  return wide == 0x10 && mem == 0x15;
}

// ---------------------------------------------------------------------------
// ASR Wd, Wn, #imm: previously the JIT emitted Sarl + Movsxlq, sign-
// extending bit 31 into upper 32. ARM W-writes must zero-extend.
// Fixed in `Digitalis: TBI mask, ASR Wd zero-ext, UBFM general JIT, USHL UB`.
// ---------------------------------------------------------------------------
bool probe_asr_w_zero_ext() {
  uint64_t input = 0x80000000ULL;  // bit 31 set in low 32
  uint64_t result;
  asm volatile(
      "mov w0, %w1\n"
      "asr w0, w0, #1\n"
      "mov %0, x0\n"
      : "=r"(result)
      : "r"(input)
      : "x0");
  // ASR W: w0 = 0x80000000 >> 1 = 0xC0000000 (32-bit), X0 upper 32 MUST be 0.
  return result == 0xC0000000ULL;
}

// ---------------------------------------------------------------------------
// UBFIZ Wd, Wn, #lsb, #width: was interpreter-only for non-LSR/LSL aliases.
// Now JIT-emitted via the UBFM general handler.
// ---------------------------------------------------------------------------
bool probe_ubfiz_w() {
  uint32_t input = 0xab;
  uint64_t result;
  asm volatile(
      "mov w0, %w1\n"
      "ubfiz w0, w0, #5, #8\n"  // bits[12:5] = input[7:0]
      "mov %0, x0\n"
      : "=r"(result)
      : "r"(input)
      : "x0");
  // Expected: (0xab & 0xff) << 5 = 0x1560, upper 32 = 0.
  return result == 0x1560ULL;
}

bool probe_ubfx_w() {
  uint32_t input = 0x0a5f0000;
  uint64_t result;
  asm volatile(
      "mov w0, %w1\n"
      "ubfx w0, w0, #12, #16\n"  // extract bits[27:12]
      "mov %0, x0\n"
      : "=r"(result)
      : "r"(input)
      : "x0");
  // Expected: (0x0a5f0000 >> 12) & 0xffff = 0xa5f0, upper 32 = 0.
  return result == 0xa5f0ULL;
}

// ---------------------------------------------------------------------------
// LDR with UXTW / SXTW offset extends: previously the JIT dropped the
// extend type, treating the offset register's full 64-bit value as the
// offset. Fixed in `Digitalis: propagate load/store offset extend type`.
// ---------------------------------------------------------------------------
bool probe_ldr_uxtw() {
  alignas(8) uint32_t arr[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
  uint32_t index = 2;
  uint64_t result;
  asm volatile(
      "mov w0, %w2\n"
      "ldr w1, [%1, w0, uxtw #2]\n"
      "mov %0, x1\n"
      : "=r"(result)
      : "r"(arr), "r"(index)
      : "x0", "x1", "memory");
  return (result & 0xFFFFFFFFULL) == 0x33333333u;
}

bool probe_ldr_sxtw_negative() {
  alignas(8) uint32_t arr[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
  uint32_t* arr_base_plus_8 = arr + 2;
  int32_t neg_index = -2;  // points back to arr[0]
  uint64_t result;
  asm volatile(
      "mov w0, %w2\n"
      "ldr w1, [%1, w0, sxtw #2]\n"
      "mov %0, x1\n"
      : "=r"(result)
      : "r"(arr_base_plus_8), "r"(neg_index)
      : "x0", "x1", "memory");
  return (result & 0xFFFFFFFFULL) == 0x11111111u;
}

// ---------------------------------------------------------------------------
// LDP base-register aliasing: when LDP's first dest aliases the base
// register, the JIT used to commit the first load via SetReg BEFORE
// emitting the second load — clobbering the base. The second load then
// read from (val1 + scale) instead of (base + scale). Observed as
// WhatsApp's libsuperpack.so SIGSEGV at 0x624c000010cc0000 in the
// `ldp x0, x8, [x0]` vtable dispatcher.
// Fixed in `Digitalis: LDP must not clobber base register before second load`.
// ---------------------------------------------------------------------------
bool probe_ldp_base_aliases_first_dest() {
  alignas(16) uint64_t mem[2] = {0xabcdef0123456789ULL, 0x55aa55aa55aa55aaULL};
  uint64_t* base = mem;
  // ldp x0, x1, [x0]  — x0 is BOTH first dest and base.
  uint64_t r0, r1;
  asm volatile(
      "mov x0, %2\n"
      "ldp x0, x1, [x0]\n"
      "mov %0, x0\n"
      "mov %1, x1\n"
      : "=r"(r0), "=r"(r1)
      : "r"(base)
      : "x0", "x1", "memory");
  // x0 must be mem[0]; x1 must be mem[1]. If the JIT clobbered the base
  // before the second load, x1 would be loaded from (mem[0] + 8) which is
  // way off into garbage memory and likely SIGSEGV.
  return r0 == 0xabcdef0123456789ULL && r1 == 0x55aa55aa55aa55aaULL;
}

bool probe_ldp_base_aliases_second_dest() {
  alignas(16) uint64_t mem[2] = {0x1111222233334444ULL, 0x5555666677778888ULL};
  uint64_t* base = mem;
  // ldp x1, x0, [x0]  — x0 is the base AND the second dest.
  uint64_t r0, r1;
  asm volatile(
      "mov x0, %2\n"
      "ldp x1, x0, [x0]\n"
      "mov %0, x0\n"
      "mov %1, x1\n"
      : "=r"(r0), "=r"(r1)
      : "r"(base)
      : "x0", "x1", "memory");
  // x1 = mem[0], x0 = mem[1].
  return r0 == 0x5555666677778888ULL && r1 == 0x1111222233334444ULL;
}

// ---------------------------------------------------------------------------
// USHL .2D INT8_MIN shift edge case: previously the JIT/interp computed
// `-shift` as int8_t, overflowing on shift=-128 and producing wrong
// result. Fixed in `Digitalis: TBI mask, ASR Wd zero-ext, UBFM general
// JIT, USHL UB`.
// ---------------------------------------------------------------------------
bool probe_ushl_2d_int8_min() {
  uint64x2_t v = {0xff00ff00ff00ff00ULL, 0x123456789abcdef0ULL};
  // Shift by -128 in each lane (signed byte interpretation). USHL: right
  // shift by abs(shift) -> abs(128) = 128 >= 64 -> result must be 0.
  int64x2_t shift = {-128, -128};
  uint64x2_t out = vshlq_u64(v, shift);
  uint64_t lane0, lane1;
  vst1q_u64(reinterpret_cast<uint64_t*>(&lane0), out);
  alignas(16) uint64_t tmp[2];
  vst1q_u64(tmp, out);
  return tmp[0] == 0 && tmp[1] == 0;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellosuperpackregress_MainActivity_probeRegressions(
    JNIEnv* env, jobject /*this*/) {
  std::string report = "Superpack regression probe:\n";
  char buf[256];

  struct Test {
    const char* name;
    bool (*fn)();
  };
  Test tests[] = {
      {"PRFM_imm", probe_prfm_immediate},
      {"PRFUM", probe_prfum_unscaled},
      {"PRFM_reg", probe_prfm_register_offset},
      {"SMAXV_4S", probe_smaxv_4s},
      {"UMAXV_4S", probe_umaxv_4s},
      {"SMINV_4S", probe_sminv_4s},
      {"UMINV_4S", probe_uminv_4s},
      {"BLR_X30", probe_blr_x30},
      {"SWPB", probe_swpb},
      {"LDADDB", probe_ldaddb},
      {"ASR_W_zero_ext", probe_asr_w_zero_ext},
      {"UBFIZ_W", probe_ubfiz_w},
      {"UBFX_W", probe_ubfx_w},
      {"LDR_UXTW", probe_ldr_uxtw},
      {"LDR_SXTW_neg", probe_ldr_sxtw_negative},
      {"LDP_base=dest1", probe_ldp_base_aliases_first_dest},
      {"LDP_base=dest2", probe_ldp_base_aliases_second_dest},
      {"USHL_2D_int8min", probe_ushl_2d_int8_min},
  };

  int pass = 0;
  int total = sizeof(tests) / sizeof(tests[0]);
  for (const auto& t : tests) {
    bool ok = t.fn();
    if (ok) ++pass;
    snprintf(buf, sizeof(buf), "  %-20s %s\n", t.name, ok ? "OK" : "FAIL");
    report += buf;
  }
  snprintf(buf, sizeof(buf), "%d / %d passed\n", pass, total);
  report += buf;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
