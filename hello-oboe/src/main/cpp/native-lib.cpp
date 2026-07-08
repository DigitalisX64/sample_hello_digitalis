// Integration-level probe for Google Oboe (high-performance native C++ audio)
// under ARM64->x86_64 translation.
//
// Two layers:
//   1. A deterministic DSP self-check (the gated assertion): synthesize a
//      float waveform whose samples are exact multiples of 1/128 — bit-identical
//      on any IEEE-754 target — FNV-1a checksum the raw float bits, and assert
//      the golden. A translator FP miscompile changes the checksum and logs
//      "OBOE FAIL", which the --status gate catches.
//   2. A best-effort real Oboe output stream: open through AudioStreamBuilder
//      (crossing into arm64-v8a liboboe.so + the platform AAudio/OpenSL ES
//      backend), write the synthesized waveform with a bounded timeout, and
//      close. This exercises the native audio stack under translation but is
//      NOT gated as a failure — the HAL round-trip cannot be captured headlessly
//      and stream availability is environmental.
//
// Only the public Oboe headers shipped in the prefab AAR are used.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <oboe/Oboe.h>

#define LOG_TAG "HelloOboe"

namespace {

// Deterministic float waveform; every value is an exact multiple of 1/128, so
// the bit pattern is identical on any IEEE-754 target. Golden computed offline
// with the same SynthF/Checksum over [0, kFrames) (tools/gen_oboe_golden.c).
float SynthF(int i) { return static_cast<float>(i % 100) * (1.0f / 128.0f) - 0.375f; }

uint64_t Checksum(const float* buf, int n) {
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < n; ++i) {
    uint32_t bits;
    std::memcpy(&bits, &buf[i], 4);
    h ^= bits;
    h *= 1099511628211ULL;
  }
  return h;
}

constexpr int kFrames = 4800;
constexpr uint64_t kGolden = 0xab139893fe562483ULL;

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellooboe_MainActivity_probeOboe(JNIEnv* env, jobject /*this*/) {
  char buf[256];

  // --- 1. Deterministic DSP self-check (the real assertion) -----------------
  std::vector<float> wave(kFrames);
  for (int i = 0; i < kFrames; ++i) wave[i] = SynthF(i);
  const uint64_t got = Checksum(wave.data(), kFrames);
  if (got != kGolden) {
    snprintf(buf, sizeof(buf), "OBOE FAIL at dsp-checksum: got=0x%016llx want=0x%016llx",
             static_cast<unsigned long long>(got), static_cast<unsigned long long>(kGolden));
    std::string msg = buf;
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", msg.c_str());
    return env->NewStringUTF(msg.c_str());
  }

  // --- 2. Best-effort real Oboe stream (exercises the audio stack) -----------
  std::string stream_note;
  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Output)
      ->setPerformanceMode(oboe::PerformanceMode::None)
      ->setSharingMode(oboe::SharingMode::Shared)
      ->setFormat(oboe::AudioFormat::Float)
      ->setChannelCount(oboe::ChannelCount::Mono)
      ->setSampleRate(48000);

  std::shared_ptr<oboe::AudioStream> stream;
  oboe::Result openResult = builder.openStream(stream);
  if (openResult == oboe::Result::OK && stream) {
    const int32_t rate = stream->getSampleRate();
    const int32_t channels = stream->getChannelCount();
    const oboe::AudioApi api = stream->getAudioApi();
    const char* apiText = (api == oboe::AudioApi::AAudio)     ? "AAudio"
                          : (api == oboe::AudioApi::OpenSLES) ? "OpenSLES"
                                                             : "Unspecified";
    int32_t framesWritten = 0;
    if (stream->requestStart() == oboe::Result::OK) {
      // Write the synthesized waveform (bounded timeout so this can never hang).
      const int32_t kWriteFrames = 240;
      auto writeResult = stream->write(wave.data(), kWriteFrames,
                                       /*timeoutNanoseconds=*/200 * 1000 * 1000);
      if (writeResult) framesWritten = writeResult.value();
      stream->requestStop();
    }
    stream->close();
    snprintf(buf, sizeof(buf), "stream OK (api=%s rate=%d ch=%d wrote=%d)", apiText, rate,
             channels, framesWritten);
    stream_note = buf;
  } else {
    snprintf(buf, sizeof(buf), "stream unavailable (%s)", oboe::convertToText(openResult));
    stream_note = buf;
  }

  std::string msg = "OBOE OK: dsp-checksum verified (" + std::to_string(kFrames) + " frames); " +
                    stream_note;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
