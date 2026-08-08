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
#include <dlfcn.h>
#include <jni.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
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

// ---------------------------------------------------------------------------
// The rest of the ANativeWindow surface.
//
// lock()/unlockAndPost() above is the convenience path. Everything below is
// what an app reaches for when it drives a Surface directly: the attribute
// setters and queries, and the dequeue/queue/cancel producer loop that carries
// fence file descriptors across the proxy boundary.
//
// Several of these are system API rather than public NDK, so they are resolved
// with dlsym and declared here -- the same thing an engine does when it wants
// the raw producer path. A symbol the host does not export is reported as a
// skip, never as a failure.
// ---------------------------------------------------------------------------

// ANativeWindowQuery (vndk/window.h). ANativeWindow_query accepts only these
// selectors and returns -EINVAL for anything else -- notably NOT the internal
// NATIVE_WINDOW_WIDTH/HEIGHT/FORMAT values, which are reachable through the
// typed getters instead.
constexpr int kQueryMinUndequeuedBuffers = 3;
constexpr int kQueryDefaultWidth = 6;
constexpr int kQueryDefaultHeight = 7;
constexpr int kQueryTransformHint = 8;
constexpr int kQueryBufferAge = 13;
constexpr int kQueryXdpi = 0x10002;

// system/window.h transform bits.
constexpr int32_t kTransformIdentity = 0;
constexpr int32_t kTransformRot90 = 4;

// ADATASPACE values (android/data_space.h), which is API 28+ only.
constexpr int32_t kDataSpaceSrgb = 0x30D10000;
constexpr int32_t kDataSpaceBt709 = 0x30C10000;

using PfnQuery = int (*)(ANativeWindow*, int, int*);
using PfnQueryf = int (*)(ANativeWindow*, int, float*);
// ANativeWindowBuffer (nativebase/nativebase.h). A dequeued buffer is this,
// not an AHardwareBuffer, so its geometry is read from the struct directly.
// `magic` is checked before anything else is believed: if the layout this
// sample was written against no longer holds, the probe skips rather than
// asserting on garbage.
constexpr int kNativeBufferMagic = ('_' << 24) | ('b' << 16) | ('f' << 8) | 'r';

struct AndroidNativeBase {
  int magic;
  int version;
  void* reserved[4];
  void (*incRef)(AndroidNativeBase*);
  void (*decRef)(AndroidNativeBase*);
};

struct NativeWindowBuffer {
  AndroidNativeBase common;
  int width;
  int height;
  int stride;
  int format;
  int usage_deprecated;
  uintptr_t layer_count;
  void* reserved[1];
  const void* handle;
  uint64_t usage;
};

using PfnDequeue = int (*)(ANativeWindow*, NativeWindowBuffer**, int*);
using PfnQueue = int (*)(ANativeWindow*, NativeWindowBuffer*, int);
using PfnCancel = int (*)(ANativeWindow*, NativeWindowBuffer*, int);
using PfnSetUsage = int (*)(ANativeWindow*, uint64_t);
using PfnSetBufferCount = int (*)(ANativeWindow*, size_t);
using PfnSetDimensions = int (*)(ANativeWindow*, uint32_t, uint32_t);
using PfnSetFormat = int (*)(ANativeWindow*, int32_t);
using PfnSetTimestamp = int (*)(ANativeWindow*, int64_t);
using PfnSetSwapInterval = int (*)(ANativeWindow*, int);
using PfnSetDequeueTimeout = int (*)(ANativeWindow*, int64_t);
using PfnSetSharedBufferMode = int (*)(ANativeWindow*, bool);
using PfnSetAutoRefresh = int (*)(ANativeWindow*, bool);
using PfnSetAutoPrerotation = int (*)(ANativeWindow*, bool);
using PfnGetLastI64 = int64_t (*)(ANativeWindow*);
using PfnSetFrameRate = int (*)(ANativeWindow*, float, int8_t);
using PfnSetFrameRateStrategy = int (*)(ANativeWindow*, float, int8_t, int8_t);
using PfnTryAllocate = void (*)(ANativeWindow*);
using PfnSetDataSpace = int32_t (*)(ANativeWindow*, int32_t);
using PfnGetDataSpace = int32_t (*)(ANativeWindow*);
using PfnSetTransform = int32_t (*)(ANativeWindow*, int32_t);

// Resolve a system-API entry point.
//
// libnativewindow.so is not in an app library's default search scope -- it is
// pulled in as a dependency of libandroid.so rather than into the global group
// -- so RTLD_DEFAULT does not find these, on a translated build and on a native
// one alike. An explicit handle is how an engine reaches them. Both are tried
// and counted separately so the sample reports which scope answered instead of
// assuming.
int g_sym_via_default = 0;
int g_sym_via_handle = 0;
int g_sym_missing = 0;

void* SymAddr(const char* name) {
  if (void* p = dlsym(RTLD_DEFAULT, name)) {
    ++g_sym_via_default;
    return p;
  }
  static void* nativewindow = dlopen("libnativewindow.so", RTLD_NOW);
  if (nativewindow != nullptr) {
    if (void* p = dlsym(nativewindow, name)) {
      ++g_sym_via_handle;
      return p;
    }
  }
  ++g_sym_missing;
  return nullptr;
}

template <typename T>
T Sym(const char* name) {
  return reinterpret_cast<T>(SymAddr(name));
}

// Records what a probe found. `fail` is a contract violation; `note` is
// something worth reporting that is not the translator's fault -- a symbol the
// platform does not export, or a setter a particular consumer refuses.
struct Report {
  std::string fails;
  int checks = 0;
  int skips = 0;

  void fail(const std::string& why) {
    ++checks;
    if (!fails.empty()) {
      fails += "; ";
    }
    fails += why;
  }
  void ok() { ++checks; }
  void skip(const char* what) {
    ++skips;
    LOGI("  skip: %s", what);
  }
  void expect(bool cond, const std::string& why) {
    if (cond) {
      ok();
    } else {
      fail(why);
    }
  }
};

// The attribute and query surface: everything that reads or writes window state
// without moving a buffer. The round trips are the real assertions -- a proxy
// that drops an out-parameter or truncates a value shows up here and nowhere
// else.
void ProbeAttributes(ANativeWindow* window, int32_t width, int32_t height, Report& r) {
  // acquire/release must be a balanced pair that leaves the window usable.
  ANativeWindow_acquire(window);
  ANativeWindow_release(window);
  r.expect(ANativeWindow_getWidth(window) > 0, "window unusable after acquire/release");

  // query(): the int out-parameter path, cross-checked against the typed
  // getters, which is the one place a dropped out-param is visible.
  auto query = Sym<PfnQuery>("ANativeWindow_query");
  if (query == nullptr) {
    r.skip("ANativeWindow_query");
  } else {
    // Every one of these must answer without error and produce a
    // non-negative value. The values themselves are the window's business: a
    // consumer that sets no default geometry legitimately reports 0.
    struct { int what; const char* name; int min; } cases[] = {
        {kQueryMinUndequeuedBuffers, "MIN_UNDEQUEUED_BUFFERS", 1},
        {kQueryDefaultWidth, "DEFAULT_WIDTH", 0},
        {kQueryDefaultHeight, "DEFAULT_HEIGHT", 0},
        {kQueryTransformHint, "TRANSFORM_HINT", 0},
        {kQueryBufferAge, "BUFFER_AGE", 0},
    };
    for (const auto& c : cases) {
      int value = INT32_MIN;
      const int rc = query(window, c.what, &value);
      r.expect(rc == 0 && value >= c.min,
               std::string("query(") + c.name + ") rc=" + std::to_string(rc) + " value=" +
                   std::to_string(value));
    }
    // The out-parameter must actually be written. Seed it with a value the
    // window cannot legitimately produce, so a query that returns success
    // without storing anything is caught.
    int sentinel = INT32_MIN;
    r.expect(query(window, kQueryMinUndequeuedBuffers, &sentinel) == 0 && sentinel != INT32_MIN,
             "query(MIN_UNDEQUEUED) reported success without writing its out-parameter");
  }

  // queryf(): the float out-parameter path. Same attribute, so the value is
  // known exactly -- a float marshalled through the wrong register or
  // truncated to int would still land near the right number, so compare
  // against the int and require equality rather than closeness.
  auto queryf = Sym<PfnQueryf>("ANativeWindow_queryf");
  if (queryf == nullptr) {
    r.skip("ANativeWindow_queryf");
  } else {
    // XDPI is returned as a float by queryf and truncated to int by query, so
    // the pair cross-checks the float return path against the int one.
    float fvalue = -1.0f;
    const int frc = queryf(window, kQueryXdpi, &fvalue);
    r.expect(frc == 0 && fvalue >= 0.0f,
             "queryf(XDPI) rc=" + std::to_string(frc) + " value=" + std::to_string(fvalue));
    if (frc == 0 && query != nullptr) {
      int ivalue = -1;
      if (query(window, kQueryXdpi, &ivalue) == 0) {
        r.expect(ivalue == static_cast<int>(fvalue),
                 "query(XDPI)=" + std::to_string(ivalue) + " != (int)queryf " +
                     std::to_string(fvalue));
      }
    }
  }

  // setBuffersDimensions + setBuffersFormat: the granular pair that apps use
  // instead of setBuffersGeometry. Setting them must be observable through the
  // getters, exactly as the combined call is.
  auto set_dimensions = Sym<PfnSetDimensions>("ANativeWindow_setBuffersDimensions");
  auto set_format = Sym<PfnSetFormat>("ANativeWindow_setBuffersFormat");
  if (set_dimensions == nullptr || set_format == nullptr) {
    r.skip("ANativeWindow_setBuffersDimensions/Format");
  } else {
    const int32_t alt_w = width / 2;
    const int32_t alt_h = height / 2;
    // These configure what the NEXT buffer is allocated as; the getters keep
    // reporting the consumer's own geometry, so the return code is the
    // contract worth asserting on.
    r.expect(set_dimensions(window, static_cast<uint32_t>(alt_w), static_cast<uint32_t>(alt_h)) == 0,
             "setBuffersDimensions failed");
    r.expect(set_format(window, AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM) == 0,
             "setBuffersFormat failed");
    // Put the window back the way it was found.
    set_dimensions(window, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    set_format(window, AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM);
  }

  // Dataspace round trip. This is the colour half of a YUV window: a dataspace
  // that does not survive the boundary shows up as a frame with the right
  // luma and wrong colour, which no crash reports.
  auto set_dataspace = Sym<PfnSetDataSpace>("ANativeWindow_setBuffersDataSpace");
  auto get_dataspace = Sym<PfnGetDataSpace>("ANativeWindow_getBuffersDataSpace");
  if (set_dataspace == nullptr || get_dataspace == nullptr) {
    r.skip("ANativeWindow_set/getBuffersDataSpace");
  } else if (set_dataspace(window, kDataSpaceBt709) == 0) {
    r.expect(get_dataspace(window) == kDataSpaceBt709, "getBuffersDataSpace != BT709 after set");
    set_dataspace(window, kDataSpaceSrgb);
    r.expect(get_dataspace(window) == kDataSpaceSrgb, "getBuffersDataSpace != sRGB after set");
  } else {
    r.skip("ANativeWindow_setBuffersDataSpace (rejected by this consumer)");
  }
  auto get_default_dataspace = Sym<PfnGetDataSpace>("ANativeWindow_getBuffersDefaultDataSpace");
  if (get_default_dataspace == nullptr) {
    r.skip("ANativeWindow_getBuffersDefaultDataSpace");
  } else {
    LOGI("  getBuffersDefaultDataSpace = 0x%x", get_default_dataspace(window));
  }

  // Transform, timestamp, usage, swap interval: setters whose contract holds
  // for any window, so a non-zero return is a real failure rather than
  // consumer policy.
  auto set_transform = Sym<PfnSetTransform>("ANativeWindow_setBuffersTransform");
  if (set_transform == nullptr) {
    r.skip("ANativeWindow_setBuffersTransform");
  } else {
    r.expect(set_transform(window, kTransformRot90) == 0, "setBuffersTransform(ROT_90) failed");
    r.expect(set_transform(window, kTransformIdentity) == 0,
             "setBuffersTransform(IDENTITY) failed");
  }

  auto set_timestamp = Sym<PfnSetTimestamp>("ANativeWindow_setBuffersTimestamp");
  if (set_timestamp == nullptr) {
    r.skip("ANativeWindow_setBuffersTimestamp");
  } else {
    // A 64-bit argument, which is where a mis-marshalled register half shows.
    r.expect(set_timestamp(window, INT64_C(0x0123456789ABCDEF)) == 0,
             "setBuffersTimestamp failed");
  }

  auto set_usage = Sym<PfnSetUsage>("ANativeWindow_setUsage");
  if (set_usage == nullptr) {
    r.skip("ANativeWindow_setUsage");
  } else {
    r.expect(set_usage(window, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                                   AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN) == 0,
             "setUsage failed");
  }

  auto set_swap = Sym<PfnSetSwapInterval>("ANativeWindow_setSwapInterval");
  if (set_swap == nullptr) {
    r.skip("ANativeWindow_setSwapInterval");
  } else {
    r.expect(set_swap(window, 1) == 0, "setSwapInterval failed");
  }

  // Setters a particular consumer may legitimately refuse: exercise the
  // marshalling, record the result, do not assert on the policy.
  auto set_count = Sym<PfnSetBufferCount>("ANativeWindow_setBufferCount");
  if (set_count == nullptr) {
    r.skip("ANativeWindow_setBufferCount");
  } else {
    LOGI("  setBufferCount(4) rc=%d", set_count(window, 4));
  }
  auto set_timeout = Sym<PfnSetDequeueTimeout>("ANativeWindow_setDequeueTimeout");
  if (set_timeout == nullptr) {
    r.skip("ANativeWindow_setDequeueTimeout");
  } else {
    LOGI("  setDequeueTimeout(1s) rc=%d", set_timeout(window, INT64_C(1000000000)));
  }
  auto set_shared = Sym<PfnSetSharedBufferMode>("ANativeWindow_setSharedBufferMode");
  if (set_shared == nullptr) {
    r.skip("ANativeWindow_setSharedBufferMode");
  } else {
    LOGI("  setSharedBufferMode(false) rc=%d", set_shared(window, false));
  }
  auto set_refresh = Sym<PfnSetAutoRefresh>("ANativeWindow_setAutoRefresh");
  if (set_refresh == nullptr) {
    r.skip("ANativeWindow_setAutoRefresh");
  } else {
    LOGI("  setAutoRefresh(false) rc=%d", set_refresh(window, false));
  }
  auto set_prerotation = Sym<PfnSetAutoPrerotation>("ANativeWindow_setAutoPrerotation");
  if (set_prerotation == nullptr) {
    r.skip("ANativeWindow_setAutoPrerotation");
  } else {
    LOGI("  setAutoPrerotation(false) rc=%d", set_prerotation(window, false));
  }

  // Float and enum arguments together. Resolved dynamically rather than
  // called directly: these are newer than this sample's minSdk, and an app
  // that wants them on an older floor reaches them the same way.
  auto set_frame_rate = Sym<PfnSetFrameRate>("ANativeWindow_setFrameRate");
  if (set_frame_rate == nullptr) {
    r.skip("ANativeWindow_setFrameRate");
  } else {
    LOGI("  setFrameRate(60) rc=%d", set_frame_rate(window, 60.0f, 0));
  }
  auto set_frame_rate_strategy =
      Sym<PfnSetFrameRateStrategy>("ANativeWindow_setFrameRateWithChangeStrategy");
  if (set_frame_rate_strategy == nullptr) {
    r.skip("ANativeWindow_setFrameRateWithChangeStrategy");
  } else {
    LOGI("  setFrameRateWithChangeStrategy(30) rc=%d",
         set_frame_rate_strategy(window, 30.0f, 0, 0));
  }

  auto try_allocate = Sym<PfnTryAllocate>("ANativeWindow_tryAllocateBuffers");
  if (try_allocate == nullptr) {
    r.skip("ANativeWindow_tryAllocateBuffers");
  } else {
    try_allocate(window);
  }

  // int64 returns. -1 means "not tracked yet", which is legal; a negative
  // value other than -1 would mean a mangled return.
  auto last_dequeue = Sym<PfnGetLastI64>("ANativeWindow_getLastDequeueDuration");
  auto last_queue = Sym<PfnGetLastI64>("ANativeWindow_getLastQueueDuration");
  auto last_start = Sym<PfnGetLastI64>("ANativeWindow_getLastDequeueStartTime");
  if (last_dequeue == nullptr || last_queue == nullptr || last_start == nullptr) {
    r.skip("ANativeWindow_getLast* timing getters");
  } else {
    const int64_t d = last_dequeue(window);
    const int64_t q = last_queue(window);
    const int64_t s = last_start(window);
    r.expect(d >= -1 && q >= -1 && s >= 0,
             "getLast* returned " + std::to_string(d) + "/" + std::to_string(q) + "/" +
                 std::to_string(s));
  }
}

// The raw producer loop: dequeue a buffer, wait on its fence, write through a
// CPU mapping, queue it back. This is the path a game engine or video renderer
// uses, and unlike lock()/unlockAndPost() it carries fence file descriptors
// across the boundary in both directions -- a class of bug this project has
// hit repeatedly.
void ProbeDequeueQueue(ANativeWindow* window,
                       int32_t width,
                       int32_t height,
                       int32_t frames,
                       Report& r) {
  auto dequeue = Sym<PfnDequeue>("ANativeWindow_dequeueBuffer");
  auto queue = Sym<PfnQueue>("ANativeWindow_queueBuffer");
  auto cancel = Sym<PfnCancel>("ANativeWindow_cancelBuffer");
  if (dequeue == nullptr || queue == nullptr || cancel == nullptr) {
    r.skip("ANativeWindow_dequeueBuffer/queueBuffer/cancelBuffer");
    return;
  }
  if (ANativeWindow_setBuffersGeometry(window, width, height,
                                       AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM) != 0) {
    r.fail("dequeue path: setBuffersGeometry failed");
    return;
  }
  // dequeueBuffer goes straight at the producer, which refuses (-ENODEV) until
  // an API is connected. There is no exported connect entry point, but lock()
  // connects as NATIVE_WINDOW_API_CPU and leaves the connection in place, so a
  // single lock/post cycle is how an app gets here from the NDK surface.
  ANativeWindow_Buffer scratch = {};
  if (ANativeWindow_lock(window, &scratch, nullptr) == 0) {
    ANativeWindow_unlockAndPost(window);
  }

  for (int32_t frame = 0; frame < frames; ++frame) {
    NativeWindowBuffer* buffer = nullptr;
    int fence = -1;
    const int rc = dequeue(window, &buffer, &fence);
    if (rc != 0 || buffer == nullptr) {
      r.fail("dequeueBuffer rc=" + std::to_string(rc));
      return;
    }
    // The fence is a real file descriptor owned by this process. Waiting on it
    // and closing it is what the producer must do; a descriptor that did not
    // survive the boundary fails here rather than leaking silently.
    if (fence >= 0) {
      struct pollfd pfd = {fence, POLLIN, 0};
      if (poll(&pfd, 1, 1000) < 0) {
        r.fail("acquire fence not pollable: errno " + std::to_string(errno));
      }
      if (close(fence) != 0) {
        r.fail("acquire fence not closeable: errno " + std::to_string(errno));
      }
    }

    if (buffer->common.magic != kNativeBufferMagic) {
      r.skip("dequeued buffer layout unrecognised (magic mismatch)");
      cancel(window, buffer, -1);
      return;
    }
    r.expect(buffer->width == width && buffer->height == height,
             "dequeued buffer is " + std::to_string(buffer->width) + "x" +
                 std::to_string(buffer->height) + ", requested " + std::to_string(width) + "x" +
                 std::to_string(height));
    r.expect(buffer->stride >= buffer->width,
             "dequeued buffer stride " + std::to_string(buffer->stride) + " < width " +
                 std::to_string(buffer->width));
    r.expect(buffer->handle != nullptr, "dequeued buffer has no native handle");

    // Alternate the two ways a buffer goes back: cancelled buffers must be
    // reusable, queued ones must reach the consumer.
    const int back = (frame == 0) ? cancel(window, buffer, -1) : queue(window, buffer, -1);
    r.expect(back == 0, std::string(frame == 0 ? "cancelBuffer" : "queueBuffer") +
                            " rc=" + std::to_string(back));
  }
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellonativewindow_MainActivity_probeApiSurface(JNIEnv* env,
                                                                jobject,
                                                                jobject surface,
                                                                jint width,
                                                                jint height,
                                                                jint frames) {
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  if (window == nullptr) {
    return env->NewStringUTF("fromSurface returned null");
  }
  Report r;
  ProbeAttributes(window, width, height, r);
  ProbeDequeueQueue(window, width, height, frames, r);
  LOGI("api-surface: symbols resolved %d via RTLD_DEFAULT, %d via libnativewindow.so handle, "
       "%d unresolved",
       g_sym_via_default,
       g_sym_via_handle,
       g_sym_missing);
  // A probe that resolved nothing proves nothing. Fail rather than report a
  // pass built entirely out of skips.
  if (g_sym_via_default + g_sym_via_handle == 0) {
    r.fail("no system-API symbol could be resolved at all");
  }
  ANativeWindow_release(window);
  if (r.fails.empty()) {
    LOGI("api-surface: OK (%d checks, %d skipped)", r.checks, r.skips);
  } else {
    LOGE("FAIL api-surface: %s", r.fails.c_str());
  }
  return env->NewStringUTF(r.fails.c_str());
}

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
