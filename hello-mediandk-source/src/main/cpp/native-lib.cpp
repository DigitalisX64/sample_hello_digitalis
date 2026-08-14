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

#include <android/log.h>
#include <fcntl.h>
#include <jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaDataSource.h>
#include <media/NdkMediaError.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#define LOG_TAG "hellomediandksource"

namespace {

constexpr int32_t kSampleRate = 44100;
constexpr int32_t kChannels = 1;
constexpr int32_t kBitRate = 64000;
constexpr int32_t kAacProfileLc = 2;
constexpr size_t kPcmSamples = 44100;      // 1 s @ 44100 Hz mono
constexpr size_t kChunkSamples = 1024;     // one AAC frame per input buffer
constexpr int64_t kPhaseDeadlineMs = 5000; // bound on every dequeue loop

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Deterministic integer sawtooth: bit-identical PCM on any target.
int16_t SynthSample(size_t i) {
  return static_cast<int16_t>(static_cast<int32_t>(i % 128) * 256 - 16384);
}

std::string Fail(const char* tag, const std::string& detail) {
  std::string m = std::string("hellomediandksource FAIL at ") + tag + ": " + detail;
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", m.c_str());
  return m;
}

#define REQUIRE(cond, tag, detail)         \
  do {                                     \
    if (!(cond)) return Fail(tag, detail); \
  } while (0)

struct EncodedFrame {
  std::vector<uint8_t> data;
  AMediaCodecBufferInfo info;
};

// In-memory data source. Callbacks are invoked by the HOST media stack on
// arbitrary threads, so the counters are atomic. This is the marshalling
// under test: guest function pointers called back from host code.
struct MemSource {
  const uint8_t* data = nullptr;
  size_t size = 0;
  bool size_unknown = false;
  std::atomic<int> read_at_calls{0};
  std::atomic<int> get_size_calls{0};
  std::atomic<int> get_avail_calls{0};
  std::atomic<int> close_calls{0};
};

ssize_t MemReadAt(void* userdata, off64_t offset, void* buffer, size_t size) {
  auto* s = static_cast<MemSource*>(userdata);
  s->read_at_calls.fetch_add(1);
  if (size == 0) return 0;
  if (offset < 0 || static_cast<uint64_t>(offset) >= s->size) return -1;
  size_t n = std::min(size, s->size - static_cast<size_t>(offset));
  memcpy(buffer, s->data + offset, n);
  return static_cast<ssize_t>(n);
}

ssize_t MemGetSize(void* userdata) {
  auto* s = static_cast<MemSource*>(userdata);
  s->get_size_calls.fetch_add(1);
  return s->size_unknown ? -1 : static_cast<ssize_t>(s->size);
}

ssize_t MemGetAvailableSize(void* userdata, off64_t offset) {
  auto* s = static_cast<MemSource*>(userdata);
  s->get_avail_calls.fetch_add(1);
  if (s->size_unknown) return -1;
  if (offset < 0 || static_cast<uint64_t>(offset) >= s->size) return 0;
  return static_cast<ssize_t>(s->size - static_cast<size_t>(offset));
}

void MemClose(void* userdata) {
  static_cast<MemSource*>(userdata)->close_calls.fetch_add(1);
}

AMediaDataSource* MakeMemDataSource(MemSource* mem) {
  AMediaDataSource* src = AMediaDataSource_new();
  if (src == nullptr) return nullptr;
  AMediaDataSource_setUserdata(src, mem);
  AMediaDataSource_setReadAt(src, MemReadAt);
  AMediaDataSource_setGetSize(src, MemGetSize);
  AMediaDataSource_setClose(src, MemClose);
  if (__builtin_available(android 29, *)) {
    AMediaDataSource_setGetAvailableSize(src, MemGetAvailableSize);
  }
  return src;
}

std::string RunProbe(const std::string& cache_dir) {
  media_status_t st;

  // --- Phase 1: encode deterministic PCM to AAC -----------------------------
  std::vector<int16_t> pcm(kPcmSamples);
  for (size_t i = 0; i < kPcmSamples; ++i) pcm[i] = SynthSample(i);

  AMediaCodec* enc = AMediaCodec_createEncoderByType("audio/mp4a-latm");
  REQUIRE(enc != nullptr, "encoder-create", "createEncoderByType returned null");

  char* enc_name = nullptr;
  if (AMediaCodec_getName(enc, &enc_name) == AMEDIA_OK && enc_name != nullptr) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "encoder: %s", enc_name);
    AMediaCodec_releaseName(enc, enc_name);
  }

  AMediaFormat* enc_fmt = AMediaFormat_new();
  REQUIRE(enc_fmt != nullptr, "encoder-format-new", "AMediaFormat_new returned null");
  AMediaFormat_setString(enc_fmt, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
  AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, kSampleRate);
  AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, kChannels);
  AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_BIT_RATE, kBitRate);
  AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_AAC_PROFILE, kAacProfileLc);
  AMediaFormat_setInt32(enc_fmt, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 16384);

  st = AMediaCodec_configure(enc, enc_fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  REQUIRE(st == AMEDIA_OK, "encoder-configure", std::to_string(st));

  AMediaFormat* in_fmt = AMediaCodec_getInputFormat(enc);
  if (in_fmt != nullptr) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "encoder input format: %s",
                        AMediaFormat_toString(in_fmt));
    AMediaFormat_delete(in_fmt);
  }

  st = AMediaCodec_start(enc);
  REQUIRE(st == AMEDIA_OK, "encoder-start", std::to_string(st));

  std::vector<EncodedFrame> frames;
  AMediaFormat* out_fmt = nullptr;
  size_t fed_samples = 0;
  bool input_done = false;
  bool output_done = false;
  int64_t deadline = NowMs() + kPhaseDeadlineMs;

  while (!output_done && NowMs() < deadline) {
    if (!input_done) {
      ssize_t idx = AMediaCodec_dequeueInputBuffer(enc, 10000);
      if (idx >= 0) {
        size_t cap = 0;
        uint8_t* in = AMediaCodec_getInputBuffer(enc, static_cast<size_t>(idx), &cap);
        REQUIRE(in != nullptr, "encoder-input-buffer", "getInputBuffer returned null");
        size_t chunk = std::min({kChunkSamples, kPcmSamples - fed_samples, cap / sizeof(int16_t)});
        memcpy(in, pcm.data() + fed_samples, chunk * sizeof(int16_t));
        uint64_t pts = fed_samples * 1000000ull / kSampleRate;
        fed_samples += chunk;
        uint32_t flags = 0;
        if (fed_samples == kPcmSamples) {
          flags = AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
          input_done = true;
        }
        st = AMediaCodec_queueInputBuffer(enc, static_cast<size_t>(idx), 0,
                                          chunk * sizeof(int16_t), pts, flags);
        REQUIRE(st == AMEDIA_OK, "encoder-queue-input", std::to_string(st));
      }
    }

    AMediaCodecBufferInfo info;
    ssize_t oidx = AMediaCodec_dequeueOutputBuffer(enc, &info, 10000);
    if (oidx >= 0) {
      if (info.size > 0 && (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) == 0) {
        size_t cap = 0;
        uint8_t* out = AMediaCodec_getOutputBuffer(enc, static_cast<size_t>(oidx), &cap);
        REQUIRE(out != nullptr, "encoder-output-buffer", "getOutputBuffer returned null");
        EncodedFrame f;
        f.data.assign(out + info.offset, out + info.offset + info.size);
        f.info = info;
        frames.push_back(std::move(f));
      }
      if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) output_done = true;
      AMediaCodec_releaseOutputBuffer(enc, static_cast<size_t>(oidx), false);
    } else if (oidx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      if (out_fmt != nullptr) AMediaFormat_delete(out_fmt);
      out_fmt = AMediaCodec_getOutputFormat(enc);
    }
    // AMEDIACODEC_INFO_TRY_AGAIN_LATER / OUTPUT_BUFFERS_CHANGED: keep looping.
  }
  REQUIRE(output_done, "encode-timeout",
          "no EOS after " + std::to_string(kPhaseDeadlineMs) + " ms; fed " +
              std::to_string(fed_samples) + "/" + std::to_string(kPcmSamples) + " samples, got " +
              std::to_string(frames.size()) + " frames");
  REQUIRE(!frames.empty(), "encode-frames", "encoder produced no frames");
  if (out_fmt == nullptr) out_fmt = AMediaCodec_getOutputFormat(enc);
  REQUIRE(out_fmt != nullptr, "encoder-output-format", "getOutputFormat returned null");
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "encoder output format: %s",
                      AMediaFormat_toString(out_fmt));

  int32_t ofmt_rate = 0;
  REQUIRE(AMediaFormat_getInt32(out_fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &ofmt_rate) &&
              ofmt_rate == kSampleRate,
          "encoder-output-rate", "sample-rate=" + std::to_string(ofmt_rate));
  void* csd = nullptr;
  size_t csd_size = 0;
  REQUIRE(AMediaFormat_getBuffer(out_fmt, AMEDIAFORMAT_KEY_CSD_0, &csd, &csd_size) &&
              csd != nullptr && csd_size >= 2,
          "encoder-csd0", "csd-0 missing or too small (size=" + std::to_string(csd_size) + ")");

  AMediaCodec_stop(enc);
  AMediaCodec_delete(enc);
  AMediaFormat_delete(enc_fmt);

  const size_t muxed_frames = frames.size();
  uint64_t muxed_bytes = 0;
  for (const EncodedFrame& f : frames) muxed_bytes += f.data.size();

  // --- Phase 2: mux to MP4 in the cache dir ---------------------------------
  const std::string path = cache_dir + "/hellomediandksource.mp4";
  int fd = open(path.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
  REQUIRE(fd >= 0, "muxer-open", path + " errno=" + std::to_string(errno));

  AMediaMuxer* mux = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
  REQUIRE(mux != nullptr, "muxer-new", "AMediaMuxer_new returned null");

  st = AMediaMuxer_setLocation(mux, 37.4f, -122.1f);
  REQUIRE(st == AMEDIA_OK, "muxer-setLocation", std::to_string(st));

  ssize_t track = AMediaMuxer_addTrack(mux, out_fmt);
  REQUIRE(track >= 0, "muxer-addTrack", std::to_string(track));

  st = AMediaMuxer_start(mux);
  REQUIRE(st == AMEDIA_OK, "muxer-start", std::to_string(st));

  for (const EncodedFrame& f : frames) {
    AMediaCodecBufferInfo wi = f.info;
    wi.offset = 0;
    wi.size = static_cast<int32_t>(f.data.size());
    st = AMediaMuxer_writeSampleData(mux, static_cast<size_t>(track), f.data.data(), &wi);
    REQUIRE(st == AMEDIA_OK, "muxer-writeSampleData",
            "pts=" + std::to_string(f.info.presentationTimeUs) + " status=" + std::to_string(st));
  }

  st = AMediaMuxer_stop(mux);
  REQUIRE(st == AMEDIA_OK, "muxer-stop", std::to_string(st));

  // Error path: writing after stop must return an error, not crash.
  {
    AMediaCodecBufferInfo dead = frames[0].info;
    dead.offset = 0;
    dead.size = static_cast<int32_t>(frames[0].data.size());
    media_status_t post =
        AMediaMuxer_writeSampleData(mux, static_cast<size_t>(track), frames[0].data.data(), &dead);
    REQUIRE(post != AMEDIA_OK, "muxer-write-after-stop", "expected error, got AMEDIA_OK");
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "writeSampleData after stop -> %d",
                        static_cast<int>(post));
  }

  st = AMediaMuxer_delete(mux);
  REQUIRE(st == AMEDIA_OK, "muxer-delete", std::to_string(st));
  close(fd);
  AMediaFormat_delete(out_fmt);

  // --- Phase 3: verify the MP4 file, load it into memory --------------------
  std::vector<uint8_t> file_data;
  {
    int rfd = open(path.c_str(), O_RDONLY);
    REQUIRE(rfd >= 0, "mp4-reopen", path + " errno=" + std::to_string(errno));
    for (;;) {
      uint8_t buf[65536];
      ssize_t n = read(rfd, buf, sizeof(buf));
      if (n <= 0) break;
      file_data.insert(file_data.end(), buf, buf + n);
    }
    close(rfd);
  }
  REQUIRE(file_data.size() > 12, "mp4-size", std::to_string(file_data.size()) + " bytes");
  REQUIRE(memcmp(file_data.data() + 4, "ftyp", 4) == 0, "mp4-ftyp",
          "no ftyp box at offset 4");

  // --- Phase 4: extract through a custom AMediaDataSource -------------------
  MemSource mem;
  mem.data = file_data.data();
  mem.size = file_data.size();
  AMediaDataSource* src = MakeMemDataSource(&mem);
  REQUIRE(src != nullptr, "datasource-new", "AMediaDataSource_new returned null");

  AMediaExtractor* ex = AMediaExtractor_new();
  REQUIRE(ex != nullptr, "extractor-new", "AMediaExtractor_new returned null");

  st = AMediaExtractor_setDataSourceCustom(ex, src);
  REQUIRE(st == AMEDIA_OK, "extractor-setDataSourceCustom", std::to_string(st));

  size_t tracks = AMediaExtractor_getTrackCount(ex);
  REQUIRE(tracks == 1, "extractor-trackCount", std::to_string(tracks));

  AMediaFormat* file_fmt = AMediaExtractor_getFileFormat(ex);
  if (file_fmt != nullptr) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "file format: %s",
                        AMediaFormat_toString(file_fmt));
    AMediaFormat_delete(file_fmt);
  }

  AMediaFormat* trk_fmt = AMediaExtractor_getTrackFormat(ex, 0);
  REQUIRE(trk_fmt != nullptr, "extractor-trackFormat", "null format for track 0");
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "track format: %s",
                      AMediaFormat_toString(trk_fmt));

  const char* mime = nullptr;
  REQUIRE(AMediaFormat_getString(trk_fmt, AMEDIAFORMAT_KEY_MIME, &mime) && mime != nullptr &&
              strcmp(mime, "audio/mp4a-latm") == 0,
          "extractor-mime", mime != nullptr ? mime : "(missing)");
  int32_t got_rate = 0;
  REQUIRE(AMediaFormat_getInt32(trk_fmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, &got_rate) &&
              got_rate == kSampleRate,
          "extractor-sampleRate", std::to_string(got_rate));
  int32_t got_ch = 0;
  REQUIRE(AMediaFormat_getInt32(trk_fmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &got_ch) &&
              got_ch == kChannels,
          "extractor-channelCount", std::to_string(got_ch));
  void* trk_csd = nullptr;
  size_t trk_csd_size = 0;
  REQUIRE(AMediaFormat_getBuffer(trk_fmt, AMEDIAFORMAT_KEY_CSD_0, &trk_csd, &trk_csd_size) &&
              trk_csd != nullptr && trk_csd_size >= 2,
          "extractor-csd0", "csd-0 missing (size=" + std::to_string(trk_csd_size) + ")");
  int64_t dur_us = 0;
  if (AMediaFormat_getInt64(trk_fmt, AMEDIAFORMAT_KEY_DURATION, &dur_us)) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "track duration: %lld us",
                        static_cast<long long>(dur_us));
  }

  st = AMediaExtractor_selectTrack(ex, 0);
  REQUIRE(st == AMEDIA_OK, "extractor-selectTrack", std::to_string(st));

  size_t extracted = 0;
  uint64_t extracted_bytes = 0;
  int64_t last_pts = -1;
  std::vector<uint8_t> sample_buf(65536);
  while (true) {
    ssize_t ssize = AMediaExtractor_getSampleSize(ex);
    if (ssize < 0) break;  // end of stream
    ssize_t n = AMediaExtractor_readSampleData(ex, sample_buf.data(), sample_buf.size());
    if (n < 0) break;
    REQUIRE(n == ssize, "extractor-sampleSize",
            "readSampleData=" + std::to_string(n) + " getSampleSize=" + std::to_string(ssize));
    int64_t pts = AMediaExtractor_getSampleTime(ex);
    REQUIRE(pts >= last_pts, "extractor-ptsOrder",
            std::to_string(pts) + " after " + std::to_string(last_pts));
    last_pts = pts;
    int ti = AMediaExtractor_getSampleTrackIndex(ex);
    REQUIRE(ti == 0, "extractor-sampleTrackIndex", std::to_string(ti));
    (void)AMediaExtractor_getSampleFlags(ex);
    ++extracted;
    extracted_bytes += static_cast<uint64_t>(n);
    REQUIRE(extracted <= 10000, "extractor-runaway", "more than 10000 samples");
    if (!AMediaExtractor_advance(ex)) break;
  }
  REQUIRE(extracted > 0, "extractor-samples", "no samples extracted");
  // MPEG4Writer trims trailing AAC frames when finalizing the moov (observed:
  // 4 of 46 dropped on the reference emulator, identically for the native
  // x86_64 build of this probe), so the tolerance is asymmetric and wide.
  REQUIRE(extracted + 8 >= muxed_frames && extracted <= muxed_frames + 2, "extractor-sampleCount",
          "extracted=" + std::to_string(extracted) + " muxed=" + std::to_string(muxed_frames));
  REQUIRE(extracted_bytes <= muxed_bytes + 4096 && extracted_bytes * 2 > muxed_bytes,
          "extractor-byteCount",
          "extracted=" + std::to_string(extracted_bytes) + " muxed=" + std::to_string(muxed_bytes));

  // The host media stack must actually have invoked the guest callbacks.
  REQUIRE(mem.read_at_calls.load() > 0, "datasource-readAt-count",
          std::to_string(mem.read_at_calls.load()));
  REQUIRE(mem.get_size_calls.load() > 0, "datasource-getSize-count",
          std::to_string(mem.get_size_calls.load()));

  // --- Phase 5: decode round-trip -------------------------------------------
  st = AMediaExtractor_seekTo(ex, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
  REQUIRE(st == AMEDIA_OK, "extractor-seekTo", std::to_string(st));

  AMediaCodec* dec = AMediaCodec_createDecoderByType(mime);
  REQUIRE(dec != nullptr, "decoder-create", mime);
  st = AMediaCodec_configure(dec, trk_fmt, nullptr, nullptr, 0);
  REQUIRE(st == AMEDIA_OK, "decoder-configure", std::to_string(st));
  st = AMediaCodec_start(dec);
  REQUIRE(st == AMEDIA_OK, "decoder-start", std::to_string(st));

  size_t decoded_frames = 0;
  uint64_t decoded_bytes = 0;
  bool dec_input_done = false;
  bool dec_output_done = false;
  deadline = NowMs() + kPhaseDeadlineMs;
  while (!dec_output_done && NowMs() < deadline) {
    if (!dec_input_done) {
      ssize_t idx = AMediaCodec_dequeueInputBuffer(dec, 10000);
      if (idx >= 0) {
        size_t cap = 0;
        uint8_t* in = AMediaCodec_getInputBuffer(dec, static_cast<size_t>(idx), &cap);
        REQUIRE(in != nullptr, "decoder-input-buffer", "getInputBuffer returned null");
        ssize_t n = AMediaExtractor_readSampleData(ex, in, cap);
        if (n < 0) {
          st = AMediaCodec_queueInputBuffer(dec, static_cast<size_t>(idx), 0, 0, 0,
                                            AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          REQUIRE(st == AMEDIA_OK, "decoder-queue-eos", std::to_string(st));
          dec_input_done = true;
        } else {
          int64_t pts = AMediaExtractor_getSampleTime(ex);
          st = AMediaCodec_queueInputBuffer(dec, static_cast<size_t>(idx), 0,
                                            static_cast<size_t>(n),
                                            pts < 0 ? 0 : static_cast<uint64_t>(pts), 0);
          REQUIRE(st == AMEDIA_OK, "decoder-queue-input", std::to_string(st));
          AMediaExtractor_advance(ex);
        }
      }
    }

    AMediaCodecBufferInfo info;
    ssize_t oidx = AMediaCodec_dequeueOutputBuffer(dec, &info, 10000);
    if (oidx >= 0) {
      if (info.size > 0) {
        ++decoded_frames;
        decoded_bytes += static_cast<uint64_t>(info.size);
      }
      if ((info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0) dec_output_done = true;
      AMediaCodec_releaseOutputBuffer(dec, static_cast<size_t>(oidx), false);
    } else if (oidx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      AMediaFormat* dfmt = AMediaCodec_getOutputFormat(dec);
      if (dfmt != nullptr) {
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "decoder output format: %s",
                            AMediaFormat_toString(dfmt));
        AMediaFormat_delete(dfmt);
      }
    }
  }
  REQUIRE(dec_output_done, "decode-timeout",
          "no EOS after " + std::to_string(kPhaseDeadlineMs) + " ms; decoded " +
              std::to_string(decoded_frames) + " frames");
  // Lossy codec: don't bit-compare, just require a plausible frame count.
  REQUIRE(decoded_frames * 10 > extracted * 8, "decode-frameCount",
          "decoded=" + std::to_string(decoded_frames) + " extracted=" + std::to_string(extracted));
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "decoded %zu frames, %llu PCM bytes",
                      decoded_frames, static_cast<unsigned long long>(decoded_bytes));

  AMediaCodec_stop(dec);
  AMediaCodec_delete(dec);
  AMediaFormat_delete(trk_fmt);
  AMediaExtractor_delete(ex);
  if (__builtin_available(android 29, *)) {
    AMediaDataSource_close(src);
  }
  AMediaDataSource_delete(src);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "datasource calls: readAt=%d getSize=%d avail=%d close=%d",
                      mem.read_at_calls.load(), mem.get_size_calls.load(),
                      mem.get_avail_calls.load(), mem.close_calls.load());

  // --- Phase 6: error path — data source with unknown size ------------------
  std::string unknown_note;
  {
    MemSource mem2;
    mem2.data = file_data.data();
    mem2.size = file_data.size();
    mem2.size_unknown = true;
    AMediaDataSource* src2 = MakeMemDataSource(&mem2);
    REQUIRE(src2 != nullptr, "datasource2-new", "AMediaDataSource_new returned null");
    AMediaExtractor* ex2 = AMediaExtractor_new();
    REQUIRE(ex2 != nullptr, "extractor2-new", "AMediaExtractor_new returned null");
    media_status_t st2 = AMediaExtractor_setDataSourceCustom(ex2, src2);
    if (st2 == AMEDIA_OK) {
      unknown_note = "unknown-size source accepted, tracks=" +
                     std::to_string(AMediaExtractor_getTrackCount(ex2));
    } else {
      unknown_note = "unknown-size source rejected cleanly, status=" + std::to_string(st2);
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s (readAt=%d getSize=%d)",
                        unknown_note.c_str(), mem2.read_at_calls.load(),
                        mem2.get_size_calls.load());
    AMediaExtractor_delete(ex2);
    AMediaDataSource_delete(src2);
  }

  unlink(path.c_str());

  return "hellomediandksource OK: encoded " + std::to_string(muxed_frames) + " frames (" +
         std::to_string(muxed_bytes) + " bytes), mp4 " + std::to_string(file_data.size()) +
         " bytes, extracted " + std::to_string(extracted) + " samples via custom source (readAt=" +
         std::to_string(mem.read_at_calls.load()) + " getSize=" +
         std::to_string(mem.get_size_calls.load()) + "), decoded " +
         std::to_string(decoded_frames) + " frames; " + unknown_note;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellomediandksource_MainActivity_probeMediaNdkSource(JNIEnv* env,
                                                                      jobject /*this*/,
                                                                      jstring cache_dir) {
  const char* dir = env->GetStringUTFChars(cache_dir, nullptr);
  std::string dir_str = dir != nullptr ? dir : "/data/local/tmp";
  if (dir != nullptr) env->ReleaseStringUTFChars(cache_dir, dir);

  std::string msg = RunProbe(dir_str);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
