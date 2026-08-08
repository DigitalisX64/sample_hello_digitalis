// Integration probe for the ANativeWindow surface API under translation.
//
// ANativeWindow is the NDK's producer-side handle on a BufferQueue: an app
// configures the geometry and pixel format, locks a buffer, writes pixels with
// the CPU, and posts it. Every step crosses the guest/host proxy boundary --
// the ANativeWindow_Buffer the guest reads is filled in by the host, and the
// pixels the guest writes have to land in a buffer the host consumer reads
// back with the same layout. A proxy that mis-marshals any part of that
// produces no crash at all; it produces wrong pixels, which is far harder to
// notice. This probe turns that into a hard pass/fail.
//
// What it checks, per (format, width, height) case:
//   1. setBuffersGeometry succeeds and getWidth/getHeight/getFormat report it
//      back (the query path, which a proxy can silently drop).
//   2. lock() fills in a USABLE ANativeWindow_Buffer: bits non-null, the
//      geometry echoed, and -- the contract that actually broke in the field --
//      a NON-ZERO stride that is at least the width. A CPU renderer addresses
//      row y at bits + y * stride, so stride == 0 collapses every row onto row
//      0 and the app's pixels never appear; for a planar YUV window that shows
//      up as a solid green (Y=U=V=0) frame with a perfectly healthy-looking UI.
//   3. The bytes written through that mapping survive the round trip to the
//      consumer. The Kotlin side reads each posted frame back off an
//      ImageReader and compares against the same pattern this file writes, so
//      a layout disagreement between producer and consumer fails the sample
//      instead of rendering as garbage nobody asserts on.
//
// The pattern is a function of (x, y, frame) only, so the consumer can
// recompute it independently rather than trusting anything the producer says.

#include <android/hardware_buffer.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdint>
#include <cstring>
#include <string>

#define LOG_TAG "hellonativewindow"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

// HAL_PIXEL_FORMAT_YV12 (system/graphics.h). Not in the public NDK header, but
// setBuffersGeometry takes the HAL value and this is the planar format real
// video apps configure.
constexpr int32_t kFormatYV12 = 0x32315659;

int32_t Align(int32_t value, int32_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

// The luma/red byte for a pixel. Kept inside [16, 235] so it is a legal
// limited-range Y value as well as a legal colour component, and so a
// zero-filled ("never written") buffer can never match it.
uint8_t PatternByte(int32_t x, int32_t y, int32_t frame) {
  return static_cast<uint8_t>(16 + ((x / 8 + y / 8 + frame * 3) % 220));
}

// Fill the locked buffer with the pattern the consumer will re-derive.
//
// For YV12 the single ANativeWindow_Buffer::stride describes the luma plane;
// the chroma planes follow the layout YV12 mandates -- V then U, each with a
// 16-byte-aligned half stride -- which is exactly the arithmetic an app has to
// do from that one field, and therefore exactly what a wrong stride breaks.
void FillPattern(const ANativeWindow_Buffer& buf, int32_t frame) {
  auto* base = static_cast<uint8_t*>(buf.bits);
  if (buf.format == kFormatYV12) {
    for (int32_t y = 0; y < buf.height; ++y) {
      uint8_t* row = base + static_cast<size_t>(y) * buf.stride;
      for (int32_t x = 0; x < buf.width; ++x) {
        row[x] = PatternByte(x, y, frame);
      }
    }
    const int32_t c_stride = Align(buf.stride / 2, 16);
    const size_t y_size = static_cast<size_t>(buf.stride) * buf.height;
    const size_t c_size = static_cast<size_t>(c_stride) * (buf.height / 2);
    // Neutral chroma: the frame reads as greyscale, so a green (all-zero YUV)
    // frame is unmistakably distinct from a correctly delivered one.
    memset(base + y_size, 0x80, c_size * 2);
    return;
  }
  const int32_t bytes_per_pixel = (buf.format == AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM) ? 2 : 4;
  for (int32_t y = 0; y < buf.height; ++y) {
    uint8_t* row = base + static_cast<size_t>(y) * buf.stride * bytes_per_pixel;
    for (int32_t x = 0; x < buf.width; ++x) {
      const uint8_t v = PatternByte(x, y, frame);
      if (bytes_per_pixel == 2) {
        // RGB_565: replicate the pattern across the channels.
        const uint16_t p = static_cast<uint16_t>(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
        memcpy(row + static_cast<size_t>(x) * 2, &p, sizeof(p));
      } else {
        uint8_t* px = row + static_cast<size_t>(x) * 4;
        px[0] = v;
        px[1] = static_cast<uint8_t>(255 - v);
        px[2] = static_cast<uint8_t>(v / 2 + 40);
        px[3] = 0xff;
      }
    }
  }
}

std::string Probe(ANativeWindow* window,
                  int32_t width,
                  int32_t height,
                  int32_t format,
                  int32_t frames,
                  bool strict_query) {
  std::string fails;
  auto fail = [&fails](const std::string& why) {
    if (!fails.empty()) {
      fails += "; ";
    }
    fails += why;
  };

  if (ANativeWindow_setBuffersGeometry(window, width, height, format) != 0) {
    return "setBuffersGeometry FAILED";
  }
  // A buffer-consumer window (ImageReader) applies the new geometry
  // immediately, so its queries are checkable. A display window resizes
  // asynchronously through the window manager, so its queries legitimately
  // still report the old size here; for those the locked buffer below is the
  // authoritative check.
  if (strict_query) {
    if (ANativeWindow_getWidth(window) != width) {
      fail("getWidth " + std::to_string(ANativeWindow_getWidth(window)) +
           " != " + std::to_string(width));
    }
    if (ANativeWindow_getHeight(window) != height) {
      fail("getHeight " + std::to_string(ANativeWindow_getHeight(window)) +
           " != " + std::to_string(height));
    }
    if (ANativeWindow_getFormat(window) != format) {
      fail("getFormat " + std::to_string(ANativeWindow_getFormat(window)) +
           " != " + std::to_string(format));
    }
  }

  for (int32_t frame = 0; frame < frames; ++frame) {
    ANativeWindow_Buffer buf = {};
    const int rc = ANativeWindow_lock(window, &buf, nullptr);
    if (rc != 0) {
      fail("lock rc=" + std::to_string(rc) + " on frame " + std::to_string(frame));
      break;
    }
    if (buf.bits == nullptr) {
      fail("lock returned null bits");
      ANativeWindow_unlockAndPost(window);
      break;
    }
    // The contract that broke in the field: a locked buffer must carry a row
    // stride the app can address rows with. Zero is unusable -- every row would
    // land on row 0 -- so there is nothing to write and nothing worth posting.
    if (buf.stride == 0) {
      fail("lock returned stride=0");
      ANativeWindow_unlockAndPost(window);
      break;
    }
    if (buf.stride < buf.width) {
      fail("stride " + std::to_string(buf.stride) + " < width " + std::to_string(buf.width));
      ANativeWindow_unlockAndPost(window);
      break;
    }
    if (buf.width != width || buf.height != height || buf.format != format) {
      fail("buffer geometry " + std::to_string(buf.width) + "x" + std::to_string(buf.height) +
           " fmt " + std::to_string(buf.format) + " != requested");
      ANativeWindow_unlockAndPost(window);
      break;
    }
    FillPattern(buf, frame);
    const int post_rc = ANativeWindow_unlockAndPost(window);
    if (post_rc != 0) {
      fail("unlockAndPost rc=" + std::to_string(post_rc));
      break;
    }
  }

  return fails;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellonativewindow_MainActivity_probeSurface(JNIEnv* env,
                                                             jobject,
                                                             jobject surface,
                                                             jint width,
                                                             jint height,
                                                             jint format,
                                                             jint frames,
                                                             jboolean strict_query) {
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) {
    LOGE("FAIL ANativeWindow_fromSurface returned null");
    return env->NewStringUTF("fromSurface returned null");
  }
  const std::string result = Probe(window, width, height, format, frames, strict_query);
  ANativeWindow_release(window);
  if (result.empty()) {
    LOGI("%dx%d fmt=0x%x: OK (%d frames posted)", width, height, format, frames);
  } else {
    LOGE("FAIL %dx%d fmt=0x%x: %s", width, height, format, result.c_str());
  }
  return env->NewStringUTF(result.c_str());
}

// Exposed so the consumer side can re-derive the expected pixels rather than
// trusting anything the producer reported.
extern "C" JNIEXPORT jint JNICALL Java_com_example_hellonativewindow_MainActivity_patternByte(
    JNIEnv*,
    jobject,
    jint x,
    jint y,
    jint frame) {
  return PatternByte(x, y, frame);
}
