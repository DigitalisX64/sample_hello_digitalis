// Exercises every AdvSIMD three-same FP vector op the Digitalis interpreter
// supports: FADD V, FSUB V, FMUL V, FMLA V, FMLS V on .4S (single) and .2D
// (double). Each maps to one of the opcodes previously listed as "Undefined"
// in DigitalisX64/platform_frameworks_libs_binary_translation#1.
//
// Uses NEON intrinsics so the emitted instructions are deterministic --
// scalar code with auto-vectorisation could be optimised away by the
// compiler at different -O levels.

#include <arm_neon.h>
#include <android/log.h>
#include <jni.h>

#include <cstdio>
#include <string>

#define LOG_TAG "hellofpvector"

namespace {

// 4-lane single-precision (.4S) — the exact form that triggered the
// "Undefined arm64 instruction 0x6e25dc03" failure for Temple Run /
// Vulkan Caps Viewer.
float32x4_t mul4s(float32x4_t a, float32x4_t b) {
  return vmulq_f32(a, b);  // FMUL Vd.4S, Vn.4S, Vm.4S
}
float32x4_t add4s(float32x4_t a, float32x4_t b) {
  return vaddq_f32(a, b);  // FADD Vd.4S, Vn.4S, Vm.4S
}
float32x4_t sub4s(float32x4_t a, float32x4_t b) {
  return vsubq_f32(a, b);  // FSUB Vd.4S, Vn.4S, Vm.4S
}
float32x4_t fma4s(float32x4_t a, float32x4_t b, float32x4_t c) {
  return vmlaq_f32(a, b, c);  // FMLA Vd.4S, Vn.4S, Vm.4S (a += b*c)
}
float32x4_t fms4s(float32x4_t a, float32x4_t b, float32x4_t c) {
  return vmlsq_f32(a, b, c);  // FMLS Vd.4S, Vn.4S, Vm.4S (a -= b*c)
}

// 2-lane double-precision (.2D) — same families, double width.
float64x2_t mul2d(float64x2_t a, float64x2_t b) { return vmulq_f64(a, b); }
float64x2_t add2d(float64x2_t a, float64x2_t b) { return vaddq_f64(a, b); }
float64x2_t sub2d(float64x2_t a, float64x2_t b) { return vsubq_f64(a, b); }

bool approx_eq(float a, float b) {
  float d = a - b;
  if (d < 0) d = -d;
  return d < 1e-4f;
}
bool approx_eq(double a, double b) {
  double d = a - b;
  if (d < 0) d = -d;
  return d < 1e-8;
}

// Read/clear FPSR via direct MRS/MSR rather than libm's fetestexcept so the
// probe doesn't depend on bionic libm's fenv being routed through guest code
// (the libm proxy is outside the Digitalis modification surface). FPSR bit
// positions per ARM ARM C5.2.8:
//   [0] IOC (invalid)        [1] DZC (div-by-zero)
//   [2] OFC (overflow)       [3] UFC (underflow)
//   [4] IXC (inexact)        [7] IDC (input denormal)
inline uint32_t read_fpsr() {
  uint64_t fpsr;
  __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
  return static_cast<uint32_t>(fpsr);
}
inline void clear_fpsr() {
  uint64_t zero = 0;
  __asm__ __volatile__("msr fpsr, %0" : : "r"(zero));
}

constexpr uint32_t kFpsrIOC = 1u << 0;  // invalid
constexpr uint32_t kFpsrDZC = 1u << 1;  // div-by-zero
constexpr uint32_t kFpsrOFC = 1u << 2;  // overflow
constexpr uint32_t kFpsrUFC = 1u << 3;  // underflow
constexpr uint32_t kFpsrIXC = 1u << 4;  // inexact

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellofpvector_MainActivity_probeFpVector(JNIEnv* env,
                                                          jobject /*this*/) {
  std::string report = "Vector FP three-same probe:\n";

  // .4S cases
  {
    alignas(16) float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    alignas(16) float b[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    alignas(16) float c[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    alignas(16) float out[4];

    float32x4_t va = vld1q_f32(a);
    float32x4_t vb = vld1q_f32(b);
    float32x4_t vc = vld1q_f32(c);

    vst1q_f32(out, mul4s(va, vb));
    bool fmul_ok = approx_eq(out[0], 5) && approx_eq(out[1], 12) &&
                   approx_eq(out[2], 21) && approx_eq(out[3], 32);

    vst1q_f32(out, add4s(va, vb));
    bool fadd_ok = approx_eq(out[0], 6) && approx_eq(out[1], 8) &&
                   approx_eq(out[2], 10) && approx_eq(out[3], 12);

    vst1q_f32(out, sub4s(vb, va));
    bool fsub_ok = approx_eq(out[0], 4) && approx_eq(out[1], 4) &&
                   approx_eq(out[2], 4) && approx_eq(out[3], 4);

    vst1q_f32(out, fma4s(vc, va, vb));  // c + a*b = 0.5 + {5, 12, 21, 32}
    bool fmla_ok = approx_eq(out[0], 5.5f) && approx_eq(out[1], 12.5f) &&
                   approx_eq(out[2], 21.5f) && approx_eq(out[3], 32.5f);

    vst1q_f32(out, fms4s(vc, va, vb));  // c - a*b
    bool fmls_ok = approx_eq(out[0], -4.5f) && approx_eq(out[1], -11.5f) &&
                   approx_eq(out[2], -20.5f) && approx_eq(out[3], -31.5f);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "  .4S  FMUL=%s FADD=%s FSUB=%s FMLA=%s FMLS=%s\n",
             fmul_ok ? "OK" : "FAIL", fadd_ok ? "OK" : "FAIL",
             fsub_ok ? "OK" : "FAIL", fmla_ok ? "OK" : "FAIL",
             fmls_ok ? "OK" : "FAIL");
    report += buf;
  }

  // .4S pairwise: FADDP / FMAXP / FMINP.  Each reduces adjacent element
  // pairs — result low half from Vn's pairs, high half from Vm's pairs.
  {
    alignas(16) float a[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    alignas(16) float b[4] = {5.0f, 6.0f, 7.0f, 8.0f};
    alignas(16) float c[4] = {1.0f, 4.0f, 2.0f, 3.0f};
    alignas(16) float d[4] = {8.0f, 5.0f, 6.0f, 7.0f};
    alignas(16) float out[4];
    float32x4_t va = vld1q_f32(a);
    float32x4_t vb = vld1q_f32(b);
    float32x4_t vc = vld1q_f32(c);
    float32x4_t vd = vld1q_f32(d);

    vst1q_f32(out, vpaddq_f32(va, vb));   // FADDP -> {3, 7, 11, 15}
    bool faddp_ok = approx_eq(out[0], 3) && approx_eq(out[1], 7) &&
                    approx_eq(out[2], 11) && approx_eq(out[3], 15);

    vst1q_f32(out, vpmaxq_f32(vc, vd));   // FMAXP -> {4, 3, 8, 7}
    bool fmaxp_ok = approx_eq(out[0], 4) && approx_eq(out[1], 3) &&
                    approx_eq(out[2], 8) && approx_eq(out[3], 7);

    vst1q_f32(out, vpminq_f32(vc, vd));   // FMINP -> {1, 2, 5, 6}
    bool fminp_ok = approx_eq(out[0], 1) && approx_eq(out[1], 2) &&
                    approx_eq(out[2], 5) && approx_eq(out[3], 6);

    char buf[256];
    snprintf(buf, sizeof(buf), "  .4Sp FADDP=%s FMAXP=%s FMINP=%s\n",
             faddp_ok ? "OK" : "FAIL", fmaxp_ok ? "OK" : "FAIL",
             fminp_ok ? "OK" : "FAIL");
    report += buf;
  }

  // .2D cases
  {
    alignas(16) double a[2] = {1.5, 2.5};
    alignas(16) double b[2] = {4.0, 8.0};
    alignas(16) double out[2];

    float64x2_t va = vld1q_f64(a);
    float64x2_t vb = vld1q_f64(b);

    vst1q_f64(out, mul2d(va, vb));
    bool fmul_ok = approx_eq(out[0], 6.0) && approx_eq(out[1], 20.0);

    vst1q_f64(out, add2d(va, vb));
    bool fadd_ok = approx_eq(out[0], 5.5) && approx_eq(out[1], 10.5);

    vst1q_f64(out, sub2d(vb, va));
    bool fsub_ok = approx_eq(out[0], 2.5) && approx_eq(out[1], 5.5);

    char buf[256];
    snprintf(buf, sizeof(buf), "  .2D  FMUL=%s FADD=%s FSUB=%s\n",
             fmul_ok ? "OK" : "FAIL", fadd_ok ? "OK" : "FAIL",
             fsub_ok ? "OK" : "FAIL");
    report += buf;
  }

  // FPSR storage round-trip — confirms MSR/MRS path is wired up
  // independent of whether any FP op actually raised an exception.
  {
    clear_fpsr();
    uint64_t set = 0xFFu;
    __asm__ __volatile__("msr fpsr, %0" : : "r"(set));
    uint32_t got = read_fpsr();
    char buf[128];
    snprintf(buf, sizeof(buf),
             "  FPSR  storage round-trip MSR=0xFF MRS=0x%X (%s)\n",
             got & 0xFFu, ((got & 0xFFu) == 0xFFu) ? "OK" : "FAIL");
    report += buf;
    clear_fpsr();
  }

  // FCVT-based interpreter-only probe — FCVT between precisions runs in
  // the interpreter (JIT comment in FpDataProc1 says "different dst
  // layouts"), so this verifies interpreter-side FP ops set FPSR.
  {
    auto fcvt_d2s = [](double a) -> float {
      float r;
      __asm__ __volatile__("fcvt %s0, %d1" : "=w"(r) : "w"(a));
      return r;
    };
    volatile float vf;
    clear_fpsr();
    // 1.0e100 narrowed to float overflows: INEXACT + OVERFLOW.
    vf = fcvt_d2s(1.0e100);
    uint32_t fpsr = read_fpsr();
    char buf[128];
    snprintf(buf, sizeof(buf),
             "  FPSR  fcvt(1e100->f) OFC=%s IXC=%s (interpreter path)\n",
             (fpsr & kFpsrOFC) ? "OK" : "FAIL",
             (fpsr & kFpsrIXC) ? "OK" : "FAIL");
    report += buf;
    (void)vf;
    clear_fpsr();
  }

  // Scalar FP exception flag probe (Plan §L1). Each subprobe clears FPSR,
  // runs a scalar FP op known to raise a specific exception, then reads
  // FPSR via MRS and checks the expected sticky bit is set. The FP ops
  // are emitted via inline asm so the C compiler can't constant-fold or
  // auto-vectorise them away.
  {
    auto fdiv = [](float a, float b) -> float {
      float r;
      __asm__ __volatile__("fdiv %s0, %s1, %s2"
                           : "=w"(r) : "w"(a), "w"(b));
      return r;
    };
    auto fmul = [](float a, float b) -> float {
      float r;
      __asm__ __volatile__("fmul %s0, %s1, %s2"
                           : "=w"(r) : "w"(a), "w"(b));
      return r;
    };
    auto fsqrt_s = [](float a) -> float {
      float r;
      __asm__ __volatile__("fsqrt %s0, %s1" : "=w"(r) : "w"(a));
      return r;
    };
    auto fsqrt_d = [](double a) -> double {
      double r;
      __asm__ __volatile__("fsqrt %d0, %d1" : "=w"(r) : "w"(a));
      return r;
    };
    auto fadd = [](float a, float b) -> float {
      float r;
      __asm__ __volatile__("fadd %s0, %s1, %s2"
                           : "=w"(r) : "w"(a), "w"(b));
      return r;
    };
    volatile float vf;
    volatile double vd;

    // INEXACT from 1.0f / 3.0f (cannot be represented in binary32).
    clear_fpsr();
    vf = fdiv(1.0f, 3.0f);
    uint32_t fpsr_inexact = read_fpsr();
    bool ixc_ok = (fpsr_inexact & kFpsrIXC) != 0;

    // INEXACT from sqrt(2.0) — irrational result, RNE rounds inexactly.
    clear_fpsr();
    vd = fsqrt_d(2.0);
    uint32_t fpsr_sqrt = read_fpsr();
    bool sqrt_ixc_ok = (fpsr_sqrt & kFpsrIXC) != 0;

    // DIVIDE-BY-ZERO from 1.0f / 0.0f.
    clear_fpsr();
    vf = fdiv(1.0f, 0.0f);
    uint32_t fpsr_dz = read_fpsr();
    bool dzc_ok = (fpsr_dz & kFpsrDZC) != 0;

    // OVERFLOW from FLT_MAX * 2.0f. Also raises INEXACT.
    clear_fpsr();
    vf = fmul(3.402823e38f, 2.0f);
    uint32_t fpsr_ov = read_fpsr();
    bool ofc_ok = (fpsr_ov & kFpsrOFC) != 0;

    // INVALID from sqrt(-1.0f).
    clear_fpsr();
    vf = fsqrt_s(-1.0f);
    uint32_t fpsr_inv = read_fpsr();
    bool ioc_ok = (fpsr_inv & kFpsrIOC) != 0;

    // FPSR persistence: a non-trapping exact op (1.0+1.0) must leave FPSR
    // cumulative-exception bits clear.
    clear_fpsr();
    vf = fadd(1.0f, 1.0f);
    uint32_t fpsr_exact = read_fpsr();
    bool exact_clean = (fpsr_exact & (kFpsrIXC | kFpsrOFC | kFpsrUFC |
                                      kFpsrDZC | kFpsrIOC)) == 0;

    char buf[512];
    snprintf(buf, sizeof(buf),
             "  FPSR  IXC(1/3)=%s  IXC(sqrt2)=%s  DZC=%s  OFC=%s  IOC=%s  "
             "exact-clean=%s\n",
             ixc_ok ? "OK" : "FAIL", sqrt_ixc_ok ? "OK" : "FAIL",
             dzc_ok ? "OK" : "FAIL", ofc_ok ? "OK" : "FAIL",
             ioc_ok ? "OK" : "FAIL", exact_clean ? "OK" : "FAIL");
    report += buf;
    (void)vf; (void)vd;
  }

  // FRINTTS (FEAT_FRINTTS): round to 32/64-bit integral FP, saturating
  // out-of-range to the most-negative value. Scalar + vector forms.
  {
    float s_in = 3.7f, s_out = 0.0f;
    __asm__ __volatile__("frint32z %s0, %s1" : "=w"(s_out) : "w"(s_in));
    bool s_ok = (s_out == 3.0f);
    float s_big = 3.0e9f, s_sat = 0.0f;  // > INT32_MAX -> INT32_MIN
    __asm__ __volatile__("frint32z %s0, %s1" : "=w"(s_sat) : "w"(s_big));
    bool s_sat_ok = (s_sat == -2147483648.0f);

    double d_in = -5.9, d_out = 0.0;
    __asm__ __volatile__("frint64z %d0, %d1" : "=w"(d_out) : "w"(d_in));
    bool d_ok = (d_out == -5.0);

    float32x4_t vin = {3.7f, -2.2f, 3.0e9f, -1.5f};
    float32x4_t vout;
    __asm__ __volatile__("frint32z %0.4s, %1.4s" : "=w"(vout) : "w"(vin));
    bool v_ok = vout[0] == 3.0f && vout[1] == -2.0f &&
                vout[2] == -2147483648.0f && vout[3] == -1.0f;

    char buf[160];
    snprintf(buf, sizeof(buf),
             "  FRINTTS scalar32=%s sat32=%s scalar64=%s vec32=%s\n",
             s_ok ? "OK" : "FAIL", s_sat_ok ? "OK" : "FAIL",
             d_ok ? "OK" : "FAIL", v_ok ? "OK" : "FAIL");
    report += buf;
  }

  // FCVTXN (FP64->FP32 narrow, round-to-odd). Exact lanes pass through; an
  // inexact lane is forced to an odd FP32 (round-to-odd).
  {
    float64x2_t din = {1.5, 2.5};
    alignas(8) float fout[2];
    vst1_f32(fout, vcvtx_f32_f64(din));
    bool exact_ok = fout[0] == 1.5f && fout[1] == 2.5f;
    // 1.0 + 2^-30 is not representable in FP32; round-to-odd makes it odd.
    float64x2_t din2 = {1.0 + 1.0 / (1 << 30), 1.0};
    alignas(8) float fout2[2];
    vst1_f32(fout2, vcvtx_f32_f64(din2));
    uint32_t b0;
    __builtin_memcpy(&b0, &fout2[0], 4);
    bool odd_ok = (b0 & 1u) == 1u && fout2[1] == 1.0f;
    char buf[96];
    snprintf(buf, sizeof(buf), "  FCVTXN exact=%s round-to-odd=%s\n",
             exact_ok ? "OK" : "FAIL", odd_ok ? "OK" : "FAIL");
    report += buf;
  }

  // FCVTL / FCVTN (FP16 <-> FP32). Narrow (FCVTN) then widen back (FCVTL):
  // exactly-representable values round-trip; an inexact value narrows with
  // round-to-nearest-even (1.1f -> 0x3C66).
  {
    float32x4_t f = {1.0f, 2.0f, -1.5f, 0.5f};
    float16x4_t h = vcvt_f16_f32(f);     // FCVTN
    float32x4_t back = vcvt_f32_f16(h);  // FCVTL
    bool exact_ok = back[0] == 1.0f && back[1] == 2.0f && back[2] == -1.5f &&
                    back[3] == 0.5f;
    float32x4_t g = {1.1f, 0.0f, 0.0f, 0.0f};
    float16x4_t gh = vcvt_f16_f32(g);
    uint16_t hb;
    __builtin_memcpy(&hb, &gh, 2);
    bool rne_ok = hb == 0x3C66;
    char buf[96];
    snprintf(buf, sizeof(buf), "  FCVT FP16 roundtrip=%s rne=%s\n",
             exact_ok ? "OK" : "FAIL", rne_ok ? "OK" : "FAIL");
    report += buf;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
