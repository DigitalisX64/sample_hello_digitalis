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

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
