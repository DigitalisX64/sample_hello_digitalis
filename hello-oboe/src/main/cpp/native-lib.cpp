// Integration-level probe for Google Oboe (high-performance native C++ audio)
// under ARM64->x86_64 translation.
//
// The probe drives Oboe's public AudioStream API end to end: it builds an
// output stream through AudioStreamBuilder, opens it (which crosses into the
// arm64-v8a liboboe.so and the platform AAudio/OpenSL ES backend), queries the
// negotiated stream properties, starts it, writes a short buffer of silence,
// and closes it. A correct run proves the native audio stack — Oboe plus the
// chosen backend — loaded and executed under translation. The emulator
// provides a working audio HAL (the same one hello-aaudio / native-audio use).
//
// Only the public Oboe headers shipped in the prefab AAR are used.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <oboe/Oboe.h>

#define LOG_TAG "HelloOboe"

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellooboe_MainActivity_probeOboe(JNIEnv* env, jobject /*this*/) {
  std::string report = "Oboe native audio probe:\n";
  char buf[256];

  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Output)
      ->setPerformanceMode(oboe::PerformanceMode::None)
      ->setSharingMode(oboe::SharingMode::Shared)
      ->setFormat(oboe::AudioFormat::Float)
      ->setChannelCount(oboe::ChannelCount::Stereo)
      ->setSampleRate(48000);

  std::shared_ptr<oboe::AudioStream> stream;
  oboe::Result openResult = builder.openStream(stream);

  if (openResult != oboe::Result::OK || !stream) {
    snprintf(buf, sizeof(buf), "OBOE could not open output stream (%s)\n",
             oboe::convertToText(openResult));
    std::string msg = buf;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
    return env->NewStringUTF(msg.c_str());
  }

  // Negotiated properties (the platform may pick its own rate / channel count).
  const int32_t rate = stream->getSampleRate();
  const int32_t channels = stream->getChannelCount();
  const oboe::AudioApi api = stream->getAudioApi();
  const char* apiText = (api == oboe::AudioApi::AAudio) ? "AAudio"
                        : (api == oboe::AudioApi::OpenSLES) ? "OpenSLES"
                                                            : "Unspecified";
  snprintf(buf, sizeof(buf), "  opened: api=%s rate=%d ch=%d\n",
           apiText, rate, channels);
  report += buf;

  // Start the stream and write a short buffer of silence through the native
  // write path. 240 frames ~= 5 ms at 48 kHz.
  bool wrote_ok = false;
  int32_t framesWritten = 0;
  oboe::Result startResult = stream->requestStart();
  if (startResult == oboe::Result::OK) {
    const int32_t kFrames = 240;
    std::vector<float> silence(static_cast<size_t>(kFrames) * channels, 0.0f);
    auto writeResult = stream->write(silence.data(), kFrames,
                                     /*timeoutNanoseconds=*/200 * 1000 * 1000);
    if (writeResult) {
      framesWritten = writeResult.value();
      wrote_ok = framesWritten >= 0;
    } else {
      snprintf(buf, sizeof(buf), "  write returned %s\n",
               oboe::convertToText(writeResult.error()));
      report += buf;
    }
    stream->requestStop();
  } else {
    snprintf(buf, sizeof(buf), "  requestStart returned %s\n",
             oboe::convertToText(startResult));
    report += buf;
  }
  stream->close();

  const bool ok = wrote_ok && rate > 0 && channels > 0;

  std::string headline;
  if (ok) {
    snprintf(buf, sizeof(buf),
             "OBOE OK (api=%s, rate=%d, ch=%d, wrote %d frames)\n",
             apiText, rate, channels, framesWritten);
    headline = buf;
  } else {
    snprintf(buf, sizeof(buf),
             "OBOE stream ran but produced no output (rate=%d ch=%d wrote=%d)\n",
             rate, channels, framesWritten);
    headline = buf;
  }

  std::string full = headline + report;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", full.c_str());
  return env->NewStringUTF(full.c_str());
}
