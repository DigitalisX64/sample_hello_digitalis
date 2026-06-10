// Integration-level probe for Tencent ncnn (native C++ neural-network
// inference) under Berberis ARM64->x86_64 translation.
//
// ncnn's CPU inference path is built out of hand-written NEON kernels (dot
// products / SGEMM for fully-connected layers, exp() for Softmax). A mistake in
// the translator's SIMD lowering would silently corrupt those kernels and yield
// wrong inference output rather than a crash, so this probe runs a tiny network
// end to end and checks the activations against hand-computed values.
//
// The network is built entirely in memory (no asset files): an Input feeds an
// InnerProduct (fully-connected) layer, whose output feeds a Softmax. With
//
//   x = [1, 2, 3]
//   W = [[0.1, 0.2, 0.3],
//        [0.4, 0.5, 0.6]]    bias = [0.5, -0.5]
//
// the InnerProduct output y = W*x + bias is
//
//   y0 = 0.1*1 + 0.2*2 + 0.3*3 + 0.5 = 1.9
//   y1 = 0.4*1 + 0.5*2 + 0.6*3 - 0.5 = 2.7
//
// and Softmax(y) = [0.310026, 0.689974] (e^-0.8 / (1 + e^-0.8), 1 / (1 + e^-0.8)).
// Vulkan is disabled so only the CPU kernels run.

#include <android/log.h>
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// ncnn's packaged CMake config sets the include dir to .../include/ncnn, so the
// headers are included without an "ncnn/" path prefix.
#include "c_api.h"  // ncnn_version()
#include "mat.h"
#include "net.h"

#define LOG_TAG "HelloNcnn"

namespace {

// Append a little-endian fp32 value to a byte buffer.
void PushF32(std::vector<unsigned char>& buf, float v) {
  unsigned char b[4];
  memcpy(b, &v, 4);
  buf.insert(buf.end(), b, b + 4);
}

// Append a little-endian uint32 value (used for the ModelBin type-0 tag word).
void PushU32(std::vector<unsigned char>& buf, uint32_t v) {
  unsigned char b[4];
  memcpy(b, &v, 4);
  buf.insert(buf.end(), b, b + 4);
}

bool ApproxEq(float a, float b) { return std::fabs(a - b) < 1e-4f; }

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloncnn_MainActivity_probeNcnn(JNIEnv* env, jobject /*this*/) {
  std::string report = "ncnn CPU inference probe:\n";
  char line[256];

  snprintf(line, sizeof(line), "  ncnn version: %s\n", ncnn_version());
  report += line;

  // --- Build the network description (modern ncnn text param format). -------
  // magic
  // layer_count blob_count
  // <type> <name> <bottom_count> <top_count> [bottoms...] [tops...] <params...>
  //
  // InnerProduct params: 0=num_output 1=bias_term 2=weight_data_size
  // Softmax param: 0=axis (0 == the single data axis for a 1-D blob)
  const char* param =
      "7767517\n"
      "3 3\n"
      "Input            in    0 1 data\n"
      "InnerProduct     fc    1 1 data fc 0=2 1=1 2=6\n"
      "Softmax          prob  1 1 fc prob 0=0\n";

  // --- Build the in-memory weights blob. ------------------------------------
  // InnerProduct::load_model reads weight_data via ModelBin::load(size, 0)
  // (a 4-byte tag then raw fp32) followed by bias_data via load(num_output, 1)
  // (raw fp32, no tag). Weight layout is row-major: weight[input_w * p + i].
  std::vector<unsigned char> weights;
  PushU32(weights, 0u);  // type-0 tag: 0 => raw fp32 weights follow
  const float W[6] = {0.1f, 0.2f, 0.3f,   // output 0
                      0.4f, 0.5f, 0.6f};  // output 1
  for (float w : W) PushF32(weights, w);
  PushF32(weights, 0.5f);   // bias[0]
  PushF32(weights, -0.5f);  // bias[1]

  ncnn::Net net;
  net.opt.use_vulkan_compute = false;  // CPU path only under translation.
  // Pin a deterministic fp32 path. ncnn detects the guest as ARMv8.2-FP16
  // capable and would otherwise store/compute the InnerProduct weights in
  // fp16, perturbing the result by ~1e-3 (an expected ncnn approximation, not
  // a translation error). Disabling fp16/bf16/packing makes the known-answer
  // test exact.
  net.opt.use_fp16_packed = false;
  net.opt.use_fp16_storage = false;
  net.opt.use_fp16_arithmetic = false;
  net.opt.use_bf16_storage = false;
  net.opt.use_packing_layout = false;

  int rc = net.load_param_mem(param);
  if (rc != 0) {
    snprintf(line, sizeof(line), "  load_param_mem rc=%d\n", rc);
    report += line;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
    return env->NewStringUTF(report.c_str());
  }

  size_t consumed = net.load_model(weights.data());
  if (consumed == 0) {
    report += "  load_model consumed 0 bytes\n";
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
    return env->NewStringUTF(report.c_str());
  }

  // --- Run inference on the known input. ------------------------------------
  ncnn::Mat in(3);
  in[0] = 1.0f;
  in[1] = 2.0f;
  in[2] = 3.0f;

  ncnn::Extractor ex = net.create_extractor();
  ex.input("data", in);

  ncnn::Mat fc_out;
  ex.extract("fc", fc_out);
  ncnn::Mat prob_out;
  ex.extract("prob", prob_out);

  const float kFcExpect[2] = {1.9f, 2.7f};
  const float kProbExpect[2] = {0.310026f, 0.689974f};

  bool fc_ok = fc_out.w == 2 && ApproxEq(fc_out[0], kFcExpect[0]) &&
               ApproxEq(fc_out[1], kFcExpect[1]);
  bool prob_ok = prob_out.w == 2 && ApproxEq(prob_out[0], kProbExpect[0]) &&
                 ApproxEq(prob_out[1], kProbExpect[1]);

  snprintf(line, sizeof(line), "  InnerProduct y=[%.4f, %.4f] (want 1.9, 2.7): %s\n",
           fc_out.w >= 1 ? fc_out[0] : 0.0f, fc_out.w >= 2 ? fc_out[1] : 0.0f,
           fc_ok ? "OK" : "MISMATCH");
  report += line;
  snprintf(line, sizeof(line),
           "  Softmax p=[%.4f, %.4f] (want 0.3100, 0.6900): %s\n",
           prob_out.w >= 1 ? prob_out[0] : 0.0f,
           prob_out.w >= 2 ? prob_out[1] : 0.0f, prob_ok ? "OK" : "MISMATCH");
  report += line;

  if (fc_ok && prob_ok) {
    snprintf(line, sizeof(line), "NCNN OK (%s, FC+Softmax match)\n", ncnn_version());
    report += line;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
