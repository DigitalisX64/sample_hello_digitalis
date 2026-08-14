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

#include <android/bitmap.h>
#include <android/data_space.h>
#include <android/imagedecoder.h>
#include <android/log.h>
#include <fcntl.h>
#include <jni.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#define LOG_TAG "helloimagedecoder"

namespace {

constexpr int kW = 64;
constexpr int kH = 48;

std::string g_report;
int g_fails = 0;

void Failf(const char* tag, const char* fmt, ...) {
  char detail[224];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail, sizeof(detail), fmt, ap);
  va_end(ap);
  char line[288];
  snprintf(line, sizeof(line), "FAIL at %s: %s", tag, detail);
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", line);
  g_report += line;
  g_report += '\n';
  ++g_fails;
}

bool CheckEq(const char* tag, long long got, long long want) {
  if (got != want) {
    Failf(tag, "got=%lld want=%lld", got, want);
    return false;
  }
  return true;
}

// Deterministic pattern, a pure function of (x, y). Alpha is always 0xFF so
// premultiplied and unpremultiplied pixel values coincide and every format
// conversion is well defined. Byte order below is RGBA_8888 memory order.
void ExpectedRGBA(int x, int y, uint8_t px[4]) {
  px[0] = static_cast<uint8_t>((x * 4) & 0xFF);        // R: ramp across x, 0..252
  px[1] = static_cast<uint8_t>((y * 5) & 0xFF);        // G: ramp across y, 0..235
  px[2] = static_cast<uint8_t>(((x + y) * 2) & 0xFF);  // B: diagonal, 0..222
  px[3] = 0xFF;
}

void VerifyRgbaExact(const char* tag, const uint8_t* buf, size_t stride, int w, int h,
                     int offX, int offY) {
  int bad = 0;
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = buf + static_cast<size_t>(y) * stride;
    for (int x = 0; x < w; ++x) {
      uint8_t want[4];
      ExpectedRGBA(x + offX, y + offY, want);
      const uint8_t* got = row + static_cast<size_t>(x) * 4;
      if (memcmp(got, want, 4) != 0) {
        if (bad == 0) {
          Failf(tag, "pixel (%d,%d) got %02x%02x%02x%02x want %02x%02x%02x%02x", x, y, got[0],
                got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
        }
        ++bad;
      }
    }
  }
  if (bad > 1) {
    Failf(tag, "%d mismatched pixels total", bad);
  }
}

bool ClearPendingException(JNIEnv* env, const char* tag) {
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    Failf(tag, "pending Java exception");
    return true;
  }
  return false;
}

// AndroidBitmap_compress write callback: a GUEST function pointer that the
// HOST-side compressor calls repeatedly. Appends each chunk to a std::vector.
bool VecWrite(void* userContext, const void* data, size_t size) {
  auto* vec = static_cast<std::vector<uint8_t>*>(userContext);
  const uint8_t* p = static_cast<const uint8_t*>(data);
  vec->insert(vec->end(), p, p + size);
  return true;
}

// Callback that refuses the first chunk: compress must observe the guest
// callback's return value and report failure instead of succeeding.
bool FailingWrite(void*, const void*, size_t) {
  return false;
}

AndroidBitmapInfo PatternInfo() {
  AndroidBitmapInfo info = {};
  info.width = kW;
  info.height = kH;
  info.stride = kW * 4;
  info.format = ANDROID_BITMAP_FORMAT_RGBA_8888;
  // Truthful: every alpha byte in the pattern is 0xFF, so declare the source
  // opaque. The PNG encoder then writes a no-alpha (color type RGB) file and
  // the decode-side header must report ALPHA_OPAQUE.
  info.flags = ANDROID_BITMAP_FLAGS_ALPHA_OPAQUE;
  return info;
}

// --- Step 1: Java Bitmap + AndroidBitmap_getInfo/lockPixels/unlockPixels ----

jobject MakePatternBitmap(JNIEnv* env, std::vector<uint8_t>* src) {
  jclass bitmapCls = env->FindClass("android/graphics/Bitmap");
  jclass configCls = env->FindClass("android/graphics/Bitmap$Config");
  if (ClearPendingException(env, "bitmap-class") || bitmapCls == nullptr ||
      configCls == nullptr) {
    return nullptr;
  }
  jfieldID argbField =
      env->GetStaticFieldID(configCls, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
  jobject argbConfig = env->GetStaticObjectField(configCls, argbField);
  jmethodID createBitmap = env->GetStaticMethodID(
      bitmapCls, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
  jobject bitmap = env->CallStaticObjectMethod(bitmapCls, createBitmap, static_cast<jint>(kW),
                                               static_cast<jint>(kH), argbConfig);
  if (ClearPendingException(env, "createBitmap") || bitmap == nullptr) {
    Failf("createBitmap", "returned null");
    return nullptr;
  }

  AndroidBitmapInfo info = {};
  if (!CheckEq("getInfo-result", AndroidBitmap_getInfo(env, bitmap, &info),
               ANDROID_BITMAP_RESULT_SUCCESS)) {
    return nullptr;
  }
  CheckEq("getInfo-width", info.width, kW);
  CheckEq("getInfo-height", info.height, kH);
  CheckEq("getInfo-format", info.format, ANDROID_BITMAP_FORMAT_RGBA_8888);
  if (info.stride < kW * 4) {
    Failf("getInfo-stride", "stride %u < %d", info.stride, kW * 4);
    return nullptr;
  }
  // createBitmap(ARGB_8888) defaults to premultiplied software storage.
  CheckEq("getInfo-alpha-flags", info.flags & ANDROID_BITMAP_FLAGS_ALPHA_MASK,
          ANDROID_BITMAP_FLAGS_ALPHA_PREMUL);
  CheckEq("getInfo-not-hardware", info.flags & ANDROID_BITMAP_FLAGS_IS_HARDWARE, 0);

  CheckEq("bitmap-dataspace", AndroidBitmap_getDataSpace(env, bitmap), ADATASPACE_SRGB);

  // A software ARGB_8888 bitmap has no hardware buffer; the call must fail
  // cleanly, not crash.
  AHardwareBuffer* hwb = nullptr;
  int hwr = AndroidBitmap_getHardwareBuffer(env, bitmap, &hwb);
  if (hwr == ANDROID_BITMAP_RESULT_SUCCESS) {
    Failf("getHardwareBuffer", "succeeded on a software bitmap");
  }

  // Fill through lockPixels, honoring the reported stride.
  void* pixels = nullptr;
  if (!CheckEq("lockPixels-result", AndroidBitmap_lockPixels(env, bitmap, &pixels),
               ANDROID_BITMAP_RESULT_SUCCESS) ||
      pixels == nullptr) {
    Failf("lockPixels", "null pixel pointer");
    return nullptr;
  }
  for (int y = 0; y < kH; ++y) {
    uint8_t* row = static_cast<uint8_t*>(pixels) + static_cast<size_t>(y) * info.stride;
    for (int x = 0; x < kW; ++x) {
      ExpectedRGBA(x, y, row + static_cast<size_t>(x) * 4);
    }
  }
  CheckEq("unlockPixels-result", AndroidBitmap_unlockPixels(env, bitmap),
          ANDROID_BITMAP_RESULT_SUCCESS);

  // Re-lock and verify the pattern survived the unlock/lock cycle, then copy
  // it out with a packed kW*4 stride for AndroidBitmap_compress.
  pixels = nullptr;
  if (!CheckEq("relock-result", AndroidBitmap_lockPixels(env, bitmap, &pixels),
               ANDROID_BITMAP_RESULT_SUCCESS) ||
      pixels == nullptr) {
    return nullptr;
  }
  VerifyRgbaExact("relock-pixels", static_cast<uint8_t*>(pixels), info.stride, kW, kH, 0, 0);
  src->resize(static_cast<size_t>(kW) * kH * 4);
  for (int y = 0; y < kH; ++y) {
    memcpy(src->data() + static_cast<size_t>(y) * kW * 4,
           static_cast<uint8_t*>(pixels) + static_cast<size_t>(y) * info.stride,
           static_cast<size_t>(kW) * 4);
  }
  CheckEq("reunlock-result", AndroidBitmap_unlockPixels(env, bitmap),
          ANDROID_BITMAP_RESULT_SUCCESS);
  return bitmap;
}

// --- Step 2: AndroidBitmap_compress (host calls the guest write callback) ---

bool CompressAll(const std::vector<uint8_t>& src, std::vector<uint8_t>* png, size_t* jpegSize,
                 size_t* webpSize) {
  const AndroidBitmapInfo info = PatternInfo();

  png->clear();
  if (!CheckEq("compress-png-result",
               AndroidBitmap_compress(&info, ADATASPACE_SRGB, src.data(),
                                      ANDROID_BITMAP_COMPRESS_FORMAT_PNG, 100, png, VecWrite),
               ANDROID_BITMAP_RESULT_SUCCESS)) {
    return false;
  }
  if (png->size() <= 8) {
    Failf("compress-png-size", "only %zu bytes", png->size());
    return false;
  }
  static const uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  if (memcmp(png->data(), kPngMagic, sizeof(kPngMagic)) != 0) {
    Failf("compress-png-magic", "first bytes %02x %02x %02x %02x", (*png)[0], (*png)[1],
          (*png)[2], (*png)[3]);
    return false;
  }

  // Lossy / other formats: assert success and non-empty output only.
  std::vector<uint8_t> jpeg;
  CheckEq("compress-jpeg-result",
          AndroidBitmap_compress(&info, ADATASPACE_SRGB, src.data(),
                                 ANDROID_BITMAP_COMPRESS_FORMAT_JPEG, 90, &jpeg, VecWrite),
          ANDROID_BITMAP_RESULT_SUCCESS);
  if (jpeg.empty()) Failf("compress-jpeg-size", "empty output");
  *jpegSize = jpeg.size();

  std::vector<uint8_t> webp;
  CheckEq("compress-webp-result",
          AndroidBitmap_compress(&info, ADATASPACE_SRGB, src.data(),
                                 ANDROID_BITMAP_COMPRESS_FORMAT_WEBP_LOSSLESS, 100, &webp,
                                 VecWrite),
          ANDROID_BITMAP_RESULT_SUCCESS);
  if (webp.empty()) Failf("compress-webp-size", "empty output");
  *webpSize = webp.size();

  // The guest callback returning false must make compress fail, not succeed.
  if (AndroidBitmap_compress(&info, ADATASPACE_SRGB, src.data(),
                             ANDROID_BITMAP_COMPRESS_FORMAT_PNG, 100, nullptr, FailingWrite) ==
      ANDROID_BITMAP_RESULT_SUCCESS) {
    Failf("compress-write-veto", "succeeded despite callback returning false");
  }
  return true;
}

// --- Step 3: full decode round trip + header walk -------------------------

AImageDecoder* MakeDecoder(const char* tag, const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = nullptr;
  int res = AImageDecoder_createFromBuffer(png.data(), png.size(), &decoder);
  if (res != ANDROID_IMAGE_DECODER_SUCCESS || decoder == nullptr) {
    Failf(tag, "createFromBuffer res=%d decoder=%p", res, static_cast<void*>(decoder));
    return nullptr;
  }
  return decoder;
}

void ProbeFullDecode(const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = MakeDecoder("full-create", png);
  if (decoder == nullptr) return;

  const AImageDecoderHeaderInfo* hi = AImageDecoder_getHeaderInfo(decoder);
  CheckEq("header-width", AImageDecoderHeaderInfo_getWidth(hi), kW);
  CheckEq("header-height", AImageDecoderHeaderInfo_getHeight(hi), kH);
  const char* mime = AImageDecoderHeaderInfo_getMimeType(hi);
  if (mime == nullptr || strcmp(mime, "image/png") != 0) {
    Failf("header-mime", "got \"%s\"", mime == nullptr ? "(null)" : mime);
  }
  // Compressed from an ALPHA_OPAQUE source, so the PNG has no alpha channel.
  CheckEq("header-alpha-flags", AImageDecoderHeaderInfo_getAlphaFlags(hi),
          ANDROID_BITMAP_FLAGS_ALPHA_OPAQUE);
  CheckEq("header-bitmap-format", AImageDecoderHeaderInfo_getAndroidBitmapFormat(hi),
          ANDROID_BITMAP_FORMAT_RGBA_8888);
  CheckEq("header-dataspace", AImageDecoderHeaderInfo_getDataSpace(hi), ADATASPACE_SRGB);

  const size_t stride = AImageDecoder_getMinimumStride(decoder);
  CheckEq("full-min-stride", static_cast<long long>(stride), kW * 4);
  const size_t size = stride * kH;
  void* pixels = malloc(size);
  if (pixels == nullptr) {
    Failf("full-alloc", "malloc(%zu)", size);
  } else {
    CheckEq("full-decode-result", AImageDecoder_decodeImage(decoder, pixels, stride, size),
            ANDROID_IMAGE_DECODER_SUCCESS);
    VerifyRgbaExact("full-roundtrip", static_cast<uint8_t*>(pixels), stride, kW, kH, 0, 0);
    free(pixels);
  }
  AImageDecoder_delete(decoder);
}

// --- Step 4: setters, each on a fresh decoder ------------------------------

void ProbeSetTargetSize(const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = MakeDecoder("scale-create", png);
  if (decoder == nullptr) return;
  const int sw = kW / 2;
  const int sh = kH / 2;
  CheckEq("scale-set-result", AImageDecoder_setTargetSize(decoder, sw, sh),
          ANDROID_IMAGE_DECODER_SUCCESS);
  const size_t stride = AImageDecoder_getMinimumStride(decoder);
  CheckEq("scale-min-stride", static_cast<long long>(stride), sw * 4);
  std::vector<uint8_t> buf(stride * sh);
  CheckEq("scale-decode-result",
          AImageDecoder_decodeImage(decoder, buf.data(), stride, buf.size()),
          ANDROID_IMAGE_DECODER_SUCCESS);
  // Plausibility, not exact filtering output: the red ramp (x) and green ramp
  // (y) must survive the 2x downscale, and alpha must remain opaque.
  auto px = [&](int x, int y) { return buf.data() + static_cast<size_t>(y) * stride + x * 4; };
  if (px(0, 0)[0] > 64) Failf("scale-red-left", "got %d want <=64", px(0, 0)[0]);
  if (px(sw - 1, 0)[0] < 180) Failf("scale-red-right", "got %d want >=180", px(sw - 1, 0)[0]);
  if (px(0, 0)[1] > 64) Failf("scale-green-top", "got %d want <=64", px(0, 0)[1]);
  if (px(0, sh - 1)[1] < 170) Failf("scale-green-bottom", "got %d want >=170", px(0, sh - 1)[1]);
  for (int corner = 0; corner < 4; ++corner) {
    const uint8_t* p = px((corner & 1) ? sw - 1 : 0, (corner & 2) ? sh - 1 : 0);
    if (p[3] != 0xFF) Failf("scale-alpha", "corner %d alpha %d", corner, p[3]);
  }
  AImageDecoder_delete(decoder);
}

void ProbeSetCrop(const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = MakeDecoder("crop-create", png);
  if (decoder == nullptr) return;
  const ARect crop = {16, 8, 48, 40};  // 32x32 region
  const int cw = crop.right - crop.left;
  const int ch = crop.bottom - crop.top;
  CheckEq("crop-set-result", AImageDecoder_setCrop(decoder, crop),
          ANDROID_IMAGE_DECODER_SUCCESS);
  const size_t stride = AImageDecoder_getMinimumStride(decoder);
  CheckEq("crop-min-stride", static_cast<long long>(stride), cw * 4);
  std::vector<uint8_t> buf(stride * ch);
  CheckEq("crop-decode-result",
          AImageDecoder_decodeImage(decoder, buf.data(), stride, buf.size()),
          ANDROID_IMAGE_DECODER_SUCCESS);
  // No scaling involved, so the crop must reproduce the pattern exactly,
  // shifted by the crop origin.
  VerifyRgbaExact("crop-roundtrip", buf.data(), stride, cw, ch, crop.left, crop.top);

  // An unsorted rect is a documented BAD_PARAMETER.
  const ARect bad = {48, 8, 16, 40};
  CheckEq("crop-unsorted-result", AImageDecoder_setCrop(decoder, bad),
          ANDROID_IMAGE_DECODER_BAD_PARAMETER);
  AImageDecoder_delete(decoder);
}

void ProbeSetFormat565(const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = MakeDecoder("565-create", png);
  if (decoder == nullptr) return;
  CheckEq("565-set-result",
          AImageDecoder_setAndroidBitmapFormat(decoder, ANDROID_BITMAP_FORMAT_RGB_565),
          ANDROID_IMAGE_DECODER_SUCCESS);
  const size_t stride = AImageDecoder_getMinimumStride(decoder);
  CheckEq("565-min-stride", static_cast<long long>(stride), kW * 2);
  std::vector<uint8_t> buf(stride * kH);
  CheckEq("565-decode-result",
          AImageDecoder_decodeImage(decoder, buf.data(), stride, buf.size()),
          ANDROID_IMAGE_DECODER_SUCCESS);
  // Allow +-1 per quantized channel to stay independent of the converter's
  // rounding mode.
  auto quant = [](int v, int bits) {
    const int m = (1 << bits) - 1;
    return (v * m + 127) / 255;
  };
  int bad = 0;
  for (int y = 0; y < kH; ++y) {
    const uint16_t* row = reinterpret_cast<const uint16_t*>(buf.data() + y * stride);
    for (int x = 0; x < kW; ++x) {
      uint8_t want[4];
      ExpectedRGBA(x, y, want);
      const int r5 = (row[x] >> 11) & 31;
      const int g6 = (row[x] >> 5) & 63;
      const int b5 = row[x] & 31;
      if (abs(r5 - quant(want[0], 5)) > 1 || abs(g6 - quant(want[1], 6)) > 1 ||
          abs(b5 - quant(want[2], 5)) > 1) {
        if (bad == 0) {
          Failf("565-pixel", "(%d,%d) got r=%d g=%d b=%d want ~r=%d g=%d b=%d", x, y, r5, g6, b5,
                quant(want[0], 5), quant(want[1], 6), quant(want[2], 5));
        }
        ++bad;
      }
    }
  }
  if (bad > 1) Failf("565-pixels", "%d mismatched pixels total", bad);
  AImageDecoder_delete(decoder);
}

void ProbeUnpremultiplied(const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = MakeDecoder("unpremul-create", png);
  if (decoder == nullptr) return;
  CheckEq("unpremul-set-result", AImageDecoder_setUnpremultipliedRequired(decoder, true),
          ANDROID_IMAGE_DECODER_SUCCESS);
  const size_t stride = AImageDecoder_getMinimumStride(decoder);
  std::vector<uint8_t> buf(stride * kH);
  CheckEq("unpremul-decode-result",
          AImageDecoder_decodeImage(decoder, buf.data(), stride, buf.size()),
          ANDROID_IMAGE_DECODER_SUCCESS);
  // Fully opaque source: unpremultiplied output equals the pattern exactly.
  VerifyRgbaExact("unpremul-roundtrip", buf.data(), stride, kW, kH, 0, 0);
  AImageDecoder_delete(decoder);
}

void ProbeSetDataSpace(const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = MakeDecoder("dataspace-create", png);
  if (decoder == nullptr) return;
  // Source is already sRGB, so requesting sRGB output is a no-op conversion
  // and the pixels must still round-trip exactly.
  CheckEq("dataspace-set-result", AImageDecoder_setDataSpace(decoder, ADATASPACE_SRGB),
          ANDROID_IMAGE_DECODER_SUCCESS);
  const size_t stride = AImageDecoder_getMinimumStride(decoder);
  std::vector<uint8_t> buf(stride * kH);
  CheckEq("dataspace-decode-result",
          AImageDecoder_decodeImage(decoder, buf.data(), stride, buf.size()),
          ANDROID_IMAGE_DECODER_SUCCESS);
  VerifyRgbaExact("dataspace-roundtrip", buf.data(), stride, kW, kH, 0, 0);
  AImageDecoder_delete(decoder);
}

void ProbeComputeSampledSize(const std::vector<uint8_t>& png) {
  AImageDecoder* decoder = MakeDecoder("sampled-create", png);
  if (decoder == nullptr) return;
  int32_t w = 0;
  int32_t h = 0;
  CheckEq("sampled2-result", AImageDecoder_computeSampledSize(decoder, 2, &w, &h),
          ANDROID_IMAGE_DECODER_SUCCESS);
  CheckEq("sampled2-width", w, kW / 2);   // 64 and 48 divide evenly: exact
  CheckEq("sampled2-height", h, kH / 2);
  CheckEq("sampled4-result", AImageDecoder_computeSampledSize(decoder, 4, &w, &h),
          ANDROID_IMAGE_DECODER_SUCCESS);
  CheckEq("sampled4-width", w, kW / 4);
  CheckEq("sampled4-height", h, kH / 4);
  CheckEq("sampled0-result", AImageDecoder_computeSampledSize(decoder, 0, &w, &h),
          ANDROID_IMAGE_DECODER_BAD_PARAMETER);
  AImageDecoder_delete(decoder);
}

// --- Step 5: createFromFd + truncated input + resultToString ---------------

void ProbeFromFd(const std::vector<uint8_t>& png, const char* cacheDir) {
  std::string path = std::string(cacheDir) + "/probe_roundtrip.png";
  int wfd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
  if (wfd < 0) {
    Failf("fd-write-open", "open(%s) errno=%d", path.c_str(), errno);
    return;
  }
  ssize_t written = write(wfd, png.data(), png.size());
  close(wfd);
  if (written != static_cast<ssize_t>(png.size())) {
    Failf("fd-write", "wrote %zd of %zu", written, png.size());
    return;
  }

  int rfd = open(path.c_str(), O_RDONLY);
  if (rfd < 0) {
    Failf("fd-read-open", "errno=%d", errno);
    return;
  }
  AImageDecoder* decoder = nullptr;
  int res = AImageDecoder_createFromFd(rfd, &decoder);
  if (res != ANDROID_IMAGE_DECODER_SUCCESS || decoder == nullptr) {
    Failf("fd-create", "res=%d", res);
  } else {
    const AImageDecoderHeaderInfo* hi = AImageDecoder_getHeaderInfo(decoder);
    CheckEq("fd-header-width", AImageDecoderHeaderInfo_getWidth(hi), kW);
    CheckEq("fd-header-height", AImageDecoderHeaderInfo_getHeight(hi), kH);
    const size_t stride = AImageDecoder_getMinimumStride(decoder);
    std::vector<uint8_t> buf(stride * kH);
    CheckEq("fd-decode-result",
            AImageDecoder_decodeImage(decoder, buf.data(), stride, buf.size()),
            ANDROID_IMAGE_DECODER_SUCCESS);
    VerifyRgbaExact("fd-roundtrip", buf.data(), stride, kW, kH, 0, 0);
    AImageDecoder_delete(decoder);
  }
  close(rfd);
  unlink(path.c_str());
}

void ProbeTruncatedInput(const std::vector<uint8_t>& png) {
  // Only the 8-byte PNG signature: creation must fail with a negative result,
  // never crash, and must not hand back a decoder.
  AImageDecoder* decoder = nullptr;
  int res = AImageDecoder_createFromBuffer(png.data(), 8, &decoder);
  if (res >= 0) Failf("truncated-magic-create", "res=%d want negative", res);
  if (decoder != nullptr) {
    Failf("truncated-magic-decoder", "non-null decoder on failure");
    AImageDecoder_delete(decoder);
  }

  // Half the file: the header is intact so creation may succeed, but the
  // decode must then report a negative error (INCOMPLETE) rather than crash.
  decoder = nullptr;
  res = AImageDecoder_createFromBuffer(png.data(), png.size() / 2, &decoder);
  if (res == ANDROID_IMAGE_DECODER_SUCCESS && decoder != nullptr) {
    const size_t stride = AImageDecoder_getMinimumStride(decoder);
    std::vector<uint8_t> buf(stride * kH);
    int dres = AImageDecoder_decodeImage(decoder, buf.data(), stride, buf.size());
    if (dres >= 0) Failf("truncated-half-decode", "res=%d want negative", dres);
    AImageDecoder_delete(decoder);
  } else if (res >= 0) {
    Failf("truncated-half-create", "res=%d without decoder", res);
  }

  if (__builtin_available(android 31, *)) {
    const char* ok = AImageDecoder_resultToString(ANDROID_IMAGE_DECODER_SUCCESS);
    if (ok == nullptr || strlen(ok) == 0) Failf("result-to-string-success", "empty");
    const char* inc = AImageDecoder_resultToString(ANDROID_IMAGE_DECODER_INCOMPLETE);
    if (inc == nullptr || strlen(inc) == 0) Failf("result-to-string-incomplete", "empty");
    if (AImageDecoder_resultToString(12345) != nullptr) {
      Failf("result-to-string-range", "non-null for out-of-range code");
    }
  }
}

// --- Step 6: animation APIs (API 31+) on the non-animated PNG --------------

void ProbeAnimationApis(const std::vector<uint8_t>& png) {
  if (!__builtin_available(android 31, *)) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "animation APIs skipped (API < 31)");
    return;
  }
  AImageDecoder* decoder = MakeDecoder("anim-create", png);
  if (decoder == nullptr) return;

  if (AImageDecoder_isAnimated(decoder)) Failf("anim-is-animated", "true for a still PNG");
  CheckEq("anim-repeat-count", AImageDecoder_getRepeatCount(decoder), 0);

  AImageDecoderFrameInfo* fi = AImageDecoderFrameInfo_create();
  if (fi == nullptr) {
    Failf("anim-frameinfo-create", "returned null");
  } else {
    CheckEq("anim-frameinfo-result", AImageDecoder_getFrameInfo(decoder, fi),
            ANDROID_IMAGE_DECODER_SUCCESS);
    const ARect rect = AImageDecoderFrameInfo_getFrameRect(fi);
    if (rect.left != 0 || rect.top != 0 || rect.right != kW || rect.bottom != kH) {
      Failf("anim-frame-rect", "got {%d,%d,%d,%d} want {0,0,%d,%d}", rect.left, rect.top,
            rect.right, rect.bottom, kW, kH);
    }
    if (AImageDecoderFrameInfo_hasAlphaWithinBounds(fi)) {
      Failf("anim-frame-alpha", "true for an opaque frame");
    }
    const int64_t duration = AImageDecoderFrameInfo_getDuration(fi);
    if (duration < 0) Failf("anim-frame-duration", "negative: %lld", (long long)duration);
    const int32_t dispose = AImageDecoderFrameInfo_getDisposeOp(fi);
    if (dispose < ANDROID_IMAGE_DECODER_DISPOSE_OP_NONE ||
        dispose > ANDROID_IMAGE_DECODER_DISPOSE_OP_PREVIOUS) {
      Failf("anim-dispose-op", "out of range: %d", dispose);
    }
    const int32_t blend = AImageDecoderFrameInfo_getBlendOp(fi);
    if (blend != ANDROID_IMAGE_DECODER_BLEND_OP_SRC &&
        blend != ANDROID_IMAGE_DECODER_BLEND_OP_SRC_OVER) {
      Failf("anim-blend-op", "out of range: %d", blend);
    }
    AImageDecoderFrameInfo_delete(fi);
  }

  // Coverage of the remaining animation entry points; a still image has no
  // next frame, so advancing can only fail (FINISHED or BAD_PARAMETER).
  const int adv = AImageDecoder_advanceFrame(decoder);
  if (adv >= 0) Failf("anim-advance", "res=%d want negative", adv);
  const int rew = AImageDecoder_rewind(decoder);
  if (rew != ANDROID_IMAGE_DECODER_SUCCESS && rew != ANDROID_IMAGE_DECODER_BAD_PARAMETER) {
    Failf("anim-rewind", "res=%d", rew);
  }
  AImageDecoder_setInternallyHandleDisposePrevious(decoder, false);
  AImageDecoder_setInternallyHandleDisposePrevious(decoder, true);
  AImageDecoder_delete(decoder);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloimagedecoder_MainActivity_probeImageDecoder(JNIEnv* env, jobject /*this*/,
                                                                  jstring cacheDirJ) {
  g_report.clear();
  g_fails = 0;

  const char* cacheDir = env->GetStringUTFChars(cacheDirJ, nullptr);

  std::vector<uint8_t> src;
  std::vector<uint8_t> png;
  size_t jpegSize = 0;
  size_t webpSize = 0;

  jobject bitmap = MakePatternBitmap(env, &src);
  if (bitmap != nullptr && g_fails == 0 && CompressAll(src, &png, &jpegSize, &webpSize)) {
    ProbeFullDecode(png);
    ProbeSetTargetSize(png);
    ProbeSetCrop(png);
    ProbeSetFormat565(png);
    ProbeUnpremultiplied(png);
    ProbeSetDataSpace(png);
    ProbeComputeSampledSize(png);
    ProbeFromFd(png, cacheDir != nullptr ? cacheDir : "/data/local/tmp");
    ProbeTruncatedInput(png);
    ProbeAnimationApis(png);
  } else if (g_fails == 0) {
    Failf("setup", "bitmap or compress setup did not complete");
  }

  if (cacheDir != nullptr) env->ReleaseStringUTFChars(cacheDirJ, cacheDir);

  std::string msg;
  if (g_fails == 0) {
    char ok[192];
    snprintf(ok, sizeof(ok),
             "helloimagedecoder OK: png=%zuB jpeg=%zuB webp=%zuB; "
             "full/scale/crop/565/unpremul/dataspace/fd decodes verified",
             png.size(), jpegSize, webpSize);
    msg = ok;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  } else {
    char head[64];
    snprintf(head, sizeof(head), "helloimagedecoder: %d check(s) failed\n", g_fails);
    msg = std::string(head) + g_report;
  }
  return env->NewStringUTF(msg.c_str());
}
