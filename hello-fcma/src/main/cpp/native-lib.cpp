// Integration-level probe for the ARMv8.3 complex-arithmetic instructions
// FCADD and FCMLA (via the <arm_neon.h> complex intrinsics vcaddq_rot90/270_f32
// and vcmlaq[_rot90/180/270]_f32).
//
// Complex numbers are stored as [re, im] pairs in adjacent FP32 lanes; a .4s
// vector holds two complex pairs. FCADD rotates one operand by +/-90 degrees and
// adds; FCMLA is a complex multiply-accumulate with one of four rotations
// (0/90/180/270) — two FCMLA calls (rot 0 + rot 90) form a full complex multiply.
// These appear in FFT/DSP kernels.
//
// The probe runs every rotation in a HOT LOOP (well past the JIT gear-up
// threshold) so the region is lowered by the heavy optimizer, and self-checks
// each SIMD result against a scalar complex-arithmetic reference. Any mismatch
// aborts() (SIGABRT) so a wrong-output miscompile is caught, and an unimplemented
// encoding SIGILLs on the first call — either way the sample "crashes" and the
// suite flags it. If all rotations stay bit-close through the hot loop, it
// returns a PASS report.

#include <android/log.h>
#include <arm_neon.h>
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define LOG_TAG "hellofcma"

namespace {

constexpr int kHotIters = 4000;  // > config::kGearSwitchThreshold (1000)

bool close4(const float* a, const float* b) {
  for (int i = 0; i < 4; i++) {
    if (std::fabs(a[i] - b[i]) > 1e-2f) return false;
  }
  return true;
}

// Scalar references (per ARM ARM C7.2.82 FCADD / C7.2.83 FCMLA), applied to each
// of the two [re, im] complex pairs in the 4-lane vector.
void ref_fcadd(const float* n, const float* m, int rot, float* out) {
  for (int p = 0; p < 2; p++) {
    const float re = n[2 * p], im = n[2 * p + 1];
    const float c = m[2 * p], d = m[2 * p + 1];
    if (rot == 90) {          // +90: rot90(c,d) = (-d, c)
      out[2 * p] = re - d;
      out[2 * p + 1] = im + c;
    } else {                  // 270: rot270(c,d) = (d, -c)
      out[2 * p] = re + d;
      out[2 * p + 1] = im - c;
    }
  }
}

void ref_fcmla(const float* acc, const float* a, const float* b, int rot,
               float* out) {
  for (int p = 0; p < 2; p++) {
    const float ar = a[2 * p], ai = a[2 * p + 1];
    const float br = b[2 * p], bi = b[2 * p + 1];
    const float cr = acc[2 * p], ci = acc[2 * p + 1];
    switch (rot) {
      case 0:   out[2 * p] = cr + ar * br; out[2 * p + 1] = ci + ar * bi; break;
      case 90:  out[2 * p] = cr - ai * bi; out[2 * p + 1] = ci + ai * br; break;
      case 180: out[2 * p] = cr - ar * br; out[2 * p + 1] = ci - ar * bi; break;
      default:  out[2 * p] = cr + ai * bi; out[2 * p + 1] = ci - ai * br; break;  // 270
    }
  }
}

// Keep each op in its own callee so its region gears up independently. `volatile`
// inputs stop the optimizer from hoisting the call out of the loop.
__attribute__((noinline)) float32x4_t op_fcadd90(float32x4_t n, float32x4_t m) {
  return vcaddq_rot90_f32(n, m);
}
__attribute__((noinline)) float32x4_t op_fcadd270(float32x4_t n, float32x4_t m) {
  return vcaddq_rot270_f32(n, m);
}
__attribute__((noinline)) float32x4_t op_fcmla0(float32x4_t r, float32x4_t a, float32x4_t b) {
  return vcmlaq_f32(r, a, b);
}
__attribute__((noinline)) float32x4_t op_fcmla90(float32x4_t r, float32x4_t a, float32x4_t b) {
  return vcmlaq_rot90_f32(r, a, b);
}
__attribute__((noinline)) float32x4_t op_fcmla180(float32x4_t r, float32x4_t a, float32x4_t b) {
  return vcmlaq_rot180_f32(r, a, b);
}
__attribute__((noinline)) float32x4_t op_fcmla270(float32x4_t r, float32x4_t a, float32x4_t b) {
  return vcmlaq_rot270_f32(r, a, b);
}

// Run one op `kHotIters` times with slightly varying inputs, checking every
// iteration. Returns true iff every iteration matched the scalar reference.
bool hot_check_fcadd(int rot) {
  for (int i = 0; i < kHotIters; i++) {
    const float f = 0.5f + static_cast<float>(i & 7);
    alignas(16) float n[4] = {1.0f * f, 2.0f, 3.0f, 4.0f * f};
    alignas(16) float m[4] = {5.0f, 6.0f * f, 7.0f * f, 8.0f};
    float32x4_t vn = vld1q_f32(n), vm = vld1q_f32(m);
    float32x4_t vr = (rot == 90) ? op_fcadd90(vn, vm) : op_fcadd270(vn, vm);
    alignas(16) float got[4];
    vst1q_f32(got, vr);
    alignas(16) float want[4];
    ref_fcadd(n, m, rot, want);
    if (!close4(got, want)) return false;
  }
  return true;
}

bool hot_check_fcmla(int rot) {
  for (int i = 0; i < kHotIters; i++) {
    const float f = 0.25f + static_cast<float>(i & 7);
    alignas(16) float acc[4] = {1.0f, 1.0f * f, 2.0f * f, 3.0f};
    alignas(16) float a[4] = {2.0f * f, 3.0f, 2.0f, 3.0f * f};
    alignas(16) float b[4] = {5.0f, 7.0f * f, 5.0f * f, 7.0f};
    float32x4_t vr = vld1q_f32(acc), va = vld1q_f32(a), vb = vld1q_f32(b);
    float32x4_t out;
    switch (rot) {
      case 0:   out = op_fcmla0(vr, va, vb); break;
      case 90:  out = op_fcmla90(vr, va, vb); break;
      case 180: out = op_fcmla180(vr, va, vb); break;
      default:  out = op_fcmla270(vr, va, vb); break;
    }
    alignas(16) float got[4];
    vst1q_f32(got, out);
    alignas(16) float want[4];
    ref_fcmla(acc, a, b, rot, want);
    if (!close4(got, want)) return false;
  }
  return true;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellofcma_MainActivity_probeFcma(JNIEnv* env, jobject /*this*/) {
  std::string report = "ARMv8.3 FCADD/FCMLA complex-arithmetic probe (heavy-tier):\n";
  char buf[128];

  struct Case { const char* name; bool ok; };
  const Case cases[] = {
      {"FCADD #90", hot_check_fcadd(90)},
      {"FCADD #270", hot_check_fcadd(270)},
      {"FCMLA #0", hot_check_fcmla(0)},
      {"FCMLA #90", hot_check_fcmla(90)},
      {"FCMLA #180", hot_check_fcmla(180)},
      {"FCMLA #270", hot_check_fcmla(270)},
  };

  bool all_ok = true;
  for (const auto& c : cases) {
    snprintf(buf, sizeof(buf), "  %-11s x%d: %s\n", c.name, kHotIters,
             c.ok ? "OK" : "FAIL");
    report += buf;
    all_ok = all_ok && c.ok;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  // A wrong result under translation is a real miscompile — crash so the sample
  // suite flags it rather than silently reporting a benign string.
  if (!all_ok) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "FCADD/FCMLA mismatch under translation; aborting");
    abort();
  }
  return env->NewStringUTF(report.c_str());
}
