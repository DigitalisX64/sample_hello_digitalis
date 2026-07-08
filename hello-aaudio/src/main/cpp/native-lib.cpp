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

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "helloaaudio"

namespace {

// Deterministic INTEGER waveform: a 100-sample sawtooth ramp. Integer-only so
// the generated PCM is bit-identical on any target — a translator arithmetic
// miscompile (wrong multiply, wrong modulo, wrong narrowing to int16) changes
// the checksum and trips the golden assertion, rather than passing silently.
int16_t SynthSample(int64_t frameIndex) {
  int32_t phase = static_cast<int32_t>(frameIndex % 100);  // 0..99
  return static_cast<int16_t>(phase * 327 - 16350);
}

// FNV-1a 64-bit over the generated PCM. Golden computed offline with the same
// SynthSample/Checksum over frames [0, kFrames); see kGolden.
uint64_t Checksum(const int16_t* buf, int n) {
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < n; ++i) {
    h ^= static_cast<uint16_t>(buf[i]);
    h *= 1099511628211ULL;
  }
  return h;
}

constexpr int kFrames = 4800;  // 0.1 s @ 48 kHz, mono
constexpr int32_t kSampleRate = 48000;
constexpr uint64_t kGolden = 0x47de127ff37aa683ULL;

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloaaudio_MainActivity_probeAAudio(JNIEnv* env, jobject /*this*/) {
  std::string msg;

  // --- Golden DSP self-check (the real assertion) ---------------------------
  int16_t buf[kFrames];
  for (int i = 0; i < kFrames; ++i) buf[i] = SynthSample(i);
  const uint64_t got = Checksum(buf, kFrames);
  if (got != kGolden) {
    char m[128];
    snprintf(m, sizeof(m), "helloaaudio FAIL at pcm-checksum: got=0x%016llx want=0x%016llx",
             static_cast<unsigned long long>(got), static_cast<unsigned long long>(kGolden));
    msg = m;
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", msg.c_str());
    return env->NewStringUTF(msg.c_str());
  }

  // --- Exercise the AAudio proxy API (builder create/configure/delete) -------
  // A real output stream is intentionally NOT opened: on a headless emulator
  // AAudioStreamBuilder_openStream/requestStart block on an audio service that
  // has no usable playback device, hanging the probe. Opening a stream is the
  // HAL round-trip the design documents as out of scope; the deterministic DSP
  // checksum above is the gated assertion. This still loads libaaudio and runs
  // the builder configuration calls through the proxy.
  std::string builder_note = "builder unavailable";
  AAudioStreamBuilder* builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) == AAUDIO_OK && builder != nullptr) {
    AAudioStreamBuilder_setSampleRate(builder, kSampleRate);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_delete(builder);
    builder_note = "builder configured";
  }

  msg = "helloaaudio OK: pcm-checksum verified (" + std::to_string(kFrames) + " frames); " +
        builder_note;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
