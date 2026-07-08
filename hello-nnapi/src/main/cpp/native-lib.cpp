/*
 * Copyright (C) 2026 utzcoz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android/NeuralNetworks.h>
#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <string>

#define LOG_TAG "hellonnapi"

namespace {

// A real NNAPI graph: out = in * kWeight + kBias, elementwise over a [4] tensor,
// built as MUL -> ADD with the weight/bias as constant operands. All values are
// exactly representable, so the expected output is bit-exact and a translator
// FP miscompile in the NNAPI reference kernel surfaces as a value mismatch.
//   in     = {1, 2, 3, 4}
//   kWeight= {0.5, 0.5, 0.5, 0.5}
//   kBias  = {1, 1, 1, 1}
//   golden = {1.5, 2.0, 2.5, 3.0}
constexpr uint32_t kLen = 4;
const float kInput[kLen] = {1.0f, 2.0f, 3.0f, 4.0f};
const float kWeight[kLen] = {0.5f, 0.5f, 0.5f, 0.5f};
const float kBias[kLen] = {1.0f, 1.0f, 1.0f, 1.0f};
const float kGolden[kLen] = {1.5f, 2.0f, 2.5f, 3.0f};

// Builds, compiles and runs the model; writes the 4 outputs into `out`.
// Returns "" on success or a "FAIL at <tag>" description.
std::string RunModel(float* out) {
  uint32_t dims[1] = {kLen};
  ANeuralNetworksOperandType tensorType{.type = ANEURALNETWORKS_TENSOR_FLOAT32,
                                        .dimensionCount = 1,
                                        .dimensions = dims,
                                        .scale = 0.0f,
                                        .zeroPoint = 0};
  ANeuralNetworksOperandType scalarType{.type = ANEURALNETWORKS_INT32,
                                        .dimensionCount = 0,
                                        .dimensions = nullptr,
                                        .scale = 0.0f,
                                        .zeroPoint = 0};

  // Operands are added in ascending index order:
  //   0 input, 1 weight(const), 2 mul-fuse(const), 3 tmp,
  //   4 bias(const), 5 add-fuse(const), 6 output.
  ANeuralNetworksModel* model = nullptr;
  if (ANeuralNetworksModel_create(&model) != ANEURALNETWORKS_NO_ERROR) return "FAIL at model-create";
  ANeuralNetworksModel_addOperand(model, &tensorType);  // 0 input
  ANeuralNetworksModel_addOperand(model, &tensorType);  // 1 weight
  ANeuralNetworksModel_addOperand(model, &scalarType);  // 2 mul fuse
  ANeuralNetworksModel_addOperand(model, &tensorType);  // 3 tmp
  ANeuralNetworksModel_addOperand(model, &tensorType);  // 4 bias
  ANeuralNetworksModel_addOperand(model, &scalarType);  // 5 add fuse
  ANeuralNetworksModel_addOperand(model, &tensorType);  // 6 output

  const int32_t fuseNone = ANEURALNETWORKS_FUSED_NONE;
  ANeuralNetworksModel_setOperandValue(model, 1, kWeight, sizeof(kWeight));
  ANeuralNetworksModel_setOperandValue(model, 2, &fuseNone, sizeof(fuseNone));
  ANeuralNetworksModel_setOperandValue(model, 4, kBias, sizeof(kBias));
  ANeuralNetworksModel_setOperandValue(model, 5, &fuseNone, sizeof(fuseNone));

  const uint32_t mulIn[3] = {0, 1, 2};
  const uint32_t mulOut[1] = {3};
  ANeuralNetworksModel_addOperation(model, ANEURALNETWORKS_MUL, 3, mulIn, 1, mulOut);
  const uint32_t addIn[3] = {3, 4, 5};
  const uint32_t addOut[1] = {6};
  ANeuralNetworksModel_addOperation(model, ANEURALNETWORKS_ADD, 3, addIn, 1, addOut);

  const uint32_t modelIn[1] = {0};
  const uint32_t modelOut[1] = {6};
  if (ANeuralNetworksModel_identifyInputsAndOutputs(model, 1, modelIn, 1, modelOut) !=
      ANEURALNETWORKS_NO_ERROR) {
    ANeuralNetworksModel_free(model);
    return "FAIL at identify-io";
  }
  if (ANeuralNetworksModel_finish(model) != ANEURALNETWORKS_NO_ERROR) {
    ANeuralNetworksModel_free(model);
    return "FAIL at model-finish";
  }

  ANeuralNetworksCompilation* compilation = nullptr;
  if (ANeuralNetworksCompilation_create(model, &compilation) != ANEURALNETWORKS_NO_ERROR ||
      ANeuralNetworksCompilation_finish(compilation) != ANEURALNETWORKS_NO_ERROR) {
    ANeuralNetworksCompilation_free(compilation);
    ANeuralNetworksModel_free(model);
    return "FAIL at compile";
  }

  ANeuralNetworksExecution* execution = nullptr;
  std::string err;
  if (ANeuralNetworksExecution_create(compilation, &execution) == ANEURALNETWORKS_NO_ERROR &&
      ANeuralNetworksExecution_setInput(execution, 0, nullptr, kInput, sizeof(kInput)) ==
          ANEURALNETWORKS_NO_ERROR &&
      ANeuralNetworksExecution_setOutput(execution, 0, nullptr, out, sizeof(float) * kLen) ==
          ANEURALNETWORKS_NO_ERROR) {
    if (ANeuralNetworksExecution_compute(execution) != ANEURALNETWORKS_NO_ERROR) {
      err = "FAIL at compute";
    }
  } else {
    err = "FAIL at execution-setup";
  }

  ANeuralNetworksExecution_free(execution);
  ANeuralNetworksCompilation_free(compilation);
  ANeuralNetworksModel_free(model);
  return err;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellonnapi_MainActivity_probeNnapi(JNIEnv* env, jobject /*this*/) {
  std::string msg;

  uint32_t deviceCount = 0;
  ANeuralNetworks_getDeviceCount(&deviceCount);

  float out[kLen] = {0, 0, 0, 0};
  const std::string err = RunModel(out);
  if (!err.empty()) {
    msg = "hellonnapi " + err;
  } else {
    std::string bad;
    for (uint32_t i = 0; i < kLen; ++i) {
      if (out[i] != kGolden[i]) {
        bad = "hellonnapi FAIL at output[" + std::to_string(i) +
              "]: got=" + std::to_string(out[i]) + " want=" + std::to_string(kGolden[i]);
        break;
      }
    }
    if (bad.empty()) {
      msg = "hellonnapi OK: MUL+ADD graph executed, output verified (devices=" +
            std::to_string(deviceCount) + ")";
    } else {
      msg = bad;
    }
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
