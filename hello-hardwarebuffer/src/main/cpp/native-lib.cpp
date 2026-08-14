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

#include <android/api-level.h>
#include <android/hardware_buffer.h>
#include <android/hardware_buffer_jni.h>
#include <android/log.h>
#include <android/rect.h>
#include <jni.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <future>
#include <string>
#include <thread>

#define LOG_TAG "hellohardwarebuffer"

namespace {

constexpr uint32_t kW = 64;
constexpr uint32_t kH = 64;
// Buffer handed to Java in the interop probe; MainActivity.kt checks the same
// dimensions through the HardwareBuffer getters.
constexpr uint32_t kJavaW = 48;
constexpr uint32_t kJavaH = 24;
constexpr uint64_t kCpuRw =
    AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;

// Deterministic pixel patterns: pure functions of coordinates, recomputed at
// verification time, so no offline golden constants are needed. Byte value is
// the low 8 bits of a small affine combination — a marshalling bug that
// shifts, truncates, or reorders buffer bytes cannot reproduce them.
uint8_t PatternA(uint32_t x, uint32_t y, uint32_t c) {
  return static_cast<uint8_t>(x * 7 + y * 13 + c * 29 + 3);
}
uint8_t PatternB(uint32_t x, uint32_t y, uint32_t c) {
  return static_cast<uint8_t>(x * 11 + y * 5 + c * 17 + 101);
}
// Rect-lock test region and the expected post-rect-write content.
constexpr int32_t kRL = 16, kRT = 16, kRR = 48, kRB = 48;
uint8_t PatternMix(uint32_t x, uint32_t y, uint32_t c) {
  const bool in = static_cast<int32_t>(x) >= kRL && static_cast<int32_t>(x) < kRR &&
                  static_cast<int32_t>(y) >= kRT && static_cast<int32_t>(y) < kRB;
  return in ? PatternB(x, y, c) : PatternA(x, y, c);
}
uint8_t PatternY(uint32_t x, uint32_t y) { return static_cast<uint8_t>(x * 3 + y * 7 + 11); }
uint8_t PatternCb(uint32_t cx, uint32_t cy) { return static_cast<uint8_t>(cx * 5 + cy * 3 + 60); }
uint8_t PatternCr(uint32_t cx, uint32_t cy) { return static_cast<uint8_t>(cx * 9 + cy * 11 + 23); }

std::string FormatV(const char* fmt, va_list ap) {
  char buf[512];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  return buf;
}

struct Probe {
  int checks = 0;
  int fails = 0;
  int skips = 0;
  std::string first_fail;

  void FailStr(const std::string& msg) {
    ++fails;
    if (first_fail.empty()) first_fail = msg;
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "FAIL at %s", msg.c_str());
  }

  __attribute__((format(printf, 2, 3))) void Fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::string msg = FormatV(fmt, ap);
    va_end(ap);
    FailStr(msg);
  }

  __attribute__((format(printf, 3, 4))) bool Expect(bool ok, const char* fmt, ...) {
    ++checks;
    if (ok) return true;
    va_list ap;
    va_start(ap, fmt);
    std::string msg = FormatV(fmt, ap);
    va_end(ap);
    FailStr(msg);
    return false;
  }

  __attribute__((format(printf, 2, 3))) void Skip(const char* fmt, ...) {
    ++skips;
    va_list ap;
    va_start(ap, fmt);
    std::string msg = FormatV(fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "SKIP %s", msg.c_str());
  }

  std::string Summary(const char* name) {
    std::string msg;
    if (fails == 0) {
      msg = std::string(name) + " OK: " + std::to_string(checks) + " checks passed, " +
            std::to_string(skips) + " skipped";
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
    } else {
      msg = std::string(name) + " FAILED: " + std::to_string(fails) + " of " +
            std::to_string(checks) + " checks failed; first: " + first_fail;
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", msg.c_str());
    }
    return msg;
  }
};

// Lock for CPU write, fill with |pat|, unlock. Returns "" or a failure detail.
std::string LockWriteRgba(AHardwareBuffer* buf, uint32_t w, uint32_t h,
                          uint8_t (*pat)(uint32_t, uint32_t, uint32_t)) {
  AHardwareBuffer_Desc d = {};
  AHardwareBuffer_describe(buf, &d);
  if (d.stride < w) return "stride " + std::to_string(d.stride) + " < width";
  void* addr = nullptr;
  int ret = AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &addr);
  if (ret != 0 || addr == nullptr) return "lock(WRITE) ret=" + std::to_string(ret);
  const uint32_t stride_bytes = d.stride * 4;
  auto* base = static_cast<uint8_t*>(addr);
  for (uint32_t y = 0; y < h; ++y) {
    uint8_t* row = base + y * stride_bytes;
    for (uint32_t x = 0; x < w; ++x)
      for (uint32_t c = 0; c < 4; ++c) row[x * 4 + c] = pat(x, y, c);
  }
  int32_t fence = -1;
  ret = AHardwareBuffer_unlock(buf, &fence);
  if (fence >= 0) close(fence);
  if (ret != 0) return "unlock after write ret=" + std::to_string(ret);
  return "";
}

// Lock for CPU read, compare every byte against |expected|, unlock.
// Returns "" or a failure detail. Thread-safe (no Probe access), so the
// socket receiver thread can use it too.
std::string VerifyRgba(AHardwareBuffer* buf, uint32_t w, uint32_t h,
                       uint8_t (*expected)(uint32_t, uint32_t, uint32_t)) {
  AHardwareBuffer_Desc d = {};
  AHardwareBuffer_describe(buf, &d);
  if (d.stride < w) return "stride " + std::to_string(d.stride) + " < width";
  void* addr = nullptr;
  int ret = AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr, &addr);
  if (ret != 0 || addr == nullptr) return "lock(READ) ret=" + std::to_string(ret);
  const uint32_t stride_bytes = d.stride * 4;
  const auto* base = static_cast<const uint8_t*>(addr);
  uint32_t bad = 0;
  char first[96] = "";
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* row = base + y * stride_bytes;
    for (uint32_t x = 0; x < w; ++x) {
      for (uint32_t c = 0; c < 4; ++c) {
        const uint8_t got = row[x * 4 + c];
        const uint8_t want = expected(x, y, c);
        if (got != want) {
          if (bad == 0) {
            snprintf(first, sizeof(first), "first (%u,%u)c%u got=0x%02x want=0x%02x", x, y, c,
                     got, want);
          }
          ++bad;
        }
      }
    }
  }
  int32_t fence = -1;
  ret = AHardwareBuffer_unlock(buf, &fence);
  if (fence >= 0) close(fence);
  if (bad != 0) return std::to_string(bad) + " mismatched bytes, " + first;
  if (ret != 0) return "unlock after read ret=" + std::to_string(ret);
  return "";
}

void FillPlane(const AHardwareBuffer_Plane& pl, uint32_t w, uint32_t h,
               uint8_t (*pat)(uint32_t, uint32_t)) {
  auto* base = static_cast<uint8_t*>(pl.data);
  for (uint32_t y = 0; y < h; ++y) {
    uint8_t* row = base + y * pl.rowStride;
    for (uint32_t x = 0; x < w; ++x) row[x * pl.pixelStride] = pat(x, y);
  }
}

std::string VerifyPlane(const AHardwareBuffer_Plane& pl, uint32_t w, uint32_t h,
                        uint8_t (*pat)(uint32_t, uint32_t)) {
  const auto* base = static_cast<const uint8_t*>(pl.data);
  uint32_t bad = 0;
  char first[96] = "";
  for (uint32_t y = 0; y < h; ++y) {
    const uint8_t* row = base + y * pl.rowStride;
    for (uint32_t x = 0; x < w; ++x) {
      const uint8_t got = row[x * pl.pixelStride];
      const uint8_t want = pat(x, y);
      if (got != want) {
        if (bad == 0) {
          snprintf(first, sizeof(first), "first (%u,%u) got=0x%02x want=0x%02x", x, y, got, want);
        }
        ++bad;
      }
    }
  }
  if (bad != 0) return std::to_string(bad) + " mismatches, " + first;
  return "";
}

// allocate / describe / isSupported / acquire / release / lock / unlock /
// rect lock / lockAndGetInfo / single-plane lockPlanes / getId.
void SectionBasics(Probe& p) {
  AHardwareBuffer_Desc want = {};
  want.width = kW;
  want.height = kH;
  want.layers = 1;
  want.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  want.usage = kCpuRw;

  if (__builtin_available(android 29, *)) {
    p.Expect(AHardwareBuffer_isSupported(&want) == 1,
             "isSupported(RGBA8888 %ux%u CPU_RW): expected 1", kW, kH);
  } else {
    p.Skip("isSupported: needs API 29, device is %d", android_get_device_api_level());
  }

  AHardwareBuffer* buf = nullptr;
  int ret = AHardwareBuffer_allocate(&want, &buf);
  if (!p.Expect(ret == 0 && buf != nullptr, "allocate RGBA8888 %ux%u: ret=%d", kW, kH, ret)) {
    return;
  }

  AHardwareBuffer_Desc got = {};
  AHardwareBuffer_describe(buf, &got);
  p.Expect(got.width == kW && got.height == kH && got.layers == 1,
           "describe dims: got %ux%u layers=%u", got.width, got.height, got.layers);
  p.Expect(got.format == AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM, "describe format: got %u",
           got.format);
  p.Expect((got.usage & kCpuRw) == kCpuRw, "describe usage: CPU_RW bits missing in 0x%" PRIx64,
           got.usage);
  p.Expect(got.stride >= kW, "describe stride: %u < width %u", got.stride, kW);

  // acquire +1 then release -1 must leave the buffer alive and usable.
  AHardwareBuffer_acquire(buf);
  AHardwareBuffer_release(buf);
  AHardwareBuffer_Desc after = {};
  AHardwareBuffer_describe(buf, &after);
  p.Expect(after.width == kW && after.height == kH,
           "describe after acquire+release: got %ux%u", after.width, after.height);

  std::string err = LockWriteRgba(buf, kW, kH, PatternA);
  p.Expect(err.empty(), "full lock/write: %s", err.c_str());
  err = VerifyRgba(buf, kW, kH, PatternA);
  p.Expect(err.empty(), "full read-back: %s", err.c_str());

  // Rect lock: write only inside the rect; outside content must survive.
  {
    ARect rect = {kRL, kRT, kRR, kRB};
    void* addr = nullptr;
    ret = AHardwareBuffer_lock(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, &rect, &addr);
    if (p.Expect(ret == 0 && addr != nullptr, "lock(WRITE, rect): ret=%d", ret)) {
      // outVirtualAddress is the buffer base; the rect is a promise about
      // which pixels are touched.
      const uint32_t stride_bytes = got.stride * 4;
      auto* base = static_cast<uint8_t*>(addr);
      for (int32_t y = kRT; y < kRB; ++y) {
        uint8_t* row = base + static_cast<uint32_t>(y) * stride_bytes;
        for (int32_t x = kRL; x < kRR; ++x)
          for (uint32_t c = 0; c < 4; ++c)
            row[static_cast<uint32_t>(x) * 4 + c] =
                PatternB(static_cast<uint32_t>(x), static_cast<uint32_t>(y), c);
      }
      int32_t fence = -1;
      ret = AHardwareBuffer_unlock(buf, &fence);
      if (fence >= 0) close(fence);
      p.Expect(ret == 0, "unlock after rect write: ret=%d", ret);
    }
    err = VerifyRgba(buf, kW, kH, PatternMix);
    p.Expect(err.empty(), "rect write containment: %s", err.c_str());
  }

  if (__builtin_available(android 29, *)) {
    void* addr = nullptr;
    int32_t bpp = -1;
    int32_t bps = -1;
    ret = AHardwareBuffer_lockAndGetInfo(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr,
                                         &addr, &bpp, &bps);
    if (ret != 0) {
      // Documented legitimate failure: mapper may not support the extra info.
      p.Skip("lockAndGetInfo: mapper does not report bpp/stride (ret=%d)", ret);
    } else {
      p.Expect(addr != nullptr, "lockAndGetInfo: null address");
      p.Expect(bpp == 4, "lockAndGetInfo bytesPerPixel: got %d want 4", bpp);
      p.Expect(bps >= static_cast<int32_t>(kW * 4),
               "lockAndGetInfo bytesPerStride: %d < %u", bps, kW * 4);
      p.Expect(bps == static_cast<int32_t>(got.stride * 4),
               "lockAndGetInfo bytesPerStride %d != describe stride*4 %u", bps, got.stride * 4);
      if (addr != nullptr && bps > 0) {
        // Spot-check one row through this mapping against the rect-mixed pattern.
        const uint32_t y = kH / 2;
        const auto* row = static_cast<const uint8_t*>(addr) + y * static_cast<uint32_t>(bps);
        uint32_t bad = 0;
        for (uint32_t x = 0; x < kW; ++x)
          for (uint32_t c = 0; c < 4; ++c)
            if (row[x * 4 + c] != PatternMix(x, y, c)) ++bad;
        p.Expect(bad == 0, "lockAndGetInfo row %u readback: %u mismatched bytes", y, bad);
      }
      int32_t fence = -1;
      ret = AHardwareBuffer_unlock(buf, &fence);
      if (fence >= 0) close(fence);
      p.Expect(ret == 0, "unlock after lockAndGetInfo: ret=%d", ret);
    }

    // lockPlanes on an RGBA buffer must report exactly one interleaved plane.
    AHardwareBuffer_Planes planes = {};
    ret = AHardwareBuffer_lockPlanes(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr,
                                     &planes);
    if (p.Expect(ret == 0, "lockPlanes(RGBA): ret=%d", ret)) {
      p.Expect(planes.planeCount == 1, "lockPlanes(RGBA) planeCount: got %u want 1",
               planes.planeCount);
      p.Expect(planes.planes[0].data != nullptr && planes.planes[0].pixelStride == 4 &&
                   planes.planes[0].rowStride >= kW * 4,
               "lockPlanes(RGBA) plane0: data=%p pixelStride=%u rowStride=%u",
               planes.planes[0].data, planes.planes[0].pixelStride, planes.planes[0].rowStride);
      int32_t fence = -1;
      ret = AHardwareBuffer_unlock(buf, &fence);
      if (fence >= 0) close(fence);
      p.Expect(ret == 0, "unlock after lockPlanes(RGBA): ret=%d", ret);
    }
  } else {
    p.Skip("lockAndGetInfo/lockPlanes: need API 29, device is %d",
           android_get_device_api_level());
  }

  if (__builtin_available(android 31, *)) {
    uint64_t id_a = 0;
    p.Expect(AHardwareBuffer_getId(buf, &id_a) == 0, "getId: nonzero return");
    AHardwareBuffer* other = nullptr;
    ret = AHardwareBuffer_allocate(&want, &other);
    if (p.Expect(ret == 0 && other != nullptr, "allocate second buffer for getId: ret=%d", ret)) {
      uint64_t id_b = 0;
      p.Expect(AHardwareBuffer_getId(other, &id_b) == 0, "getId(second): nonzero return");
      p.Expect(id_a != id_b, "getId uniqueness: both buffers report 0x%" PRIx64, id_a);
      AHardwareBuffer_release(other);
    }
  } else {
    p.Skip("getId: needs API 31, device is %d", android_get_device_api_level());
  }

  AHardwareBuffer_release(buf);
}

// lockPlanes on a YUV 420 buffer: 3 planes (Y, Cb, Cr), sane strides,
// write/read round-trip through the plane pointers.
void SectionYuvPlanes(Probe& p) {
  if (__builtin_available(android 29, *)) {
    AHardwareBuffer_Desc d = {};
    d.width = kW;
    d.height = kH;
    d.layers = 1;
    d.format = AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420;
    d.usage = kCpuRw;
    if (AHardwareBuffer_isSupported(&d) != 1) {
      p.Skip("YUV Y8Cb8Cr8_420 %ux%u CPU_RW: not allocatable on this gralloc", kW, kH);
      return;
    }
    AHardwareBuffer* buf = nullptr;
    int ret = AHardwareBuffer_allocate(&d, &buf);
    if (!p.Expect(ret == 0 && buf != nullptr, "allocate YUV: ret=%d (isSupported said 1)", ret)) {
      return;
    }

    const uint32_t cw = kW / 2;
    const uint32_t ch = kH / 2;
    AHardwareBuffer_Planes planes = {};
    ret = AHardwareBuffer_lockPlanes(buf, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr,
                                     &planes);
    if (!p.Expect(ret == 0, "lockPlanes(YUV, WRITE): ret=%d", ret)) {
      AHardwareBuffer_release(buf);
      return;
    }
    bool layout_ok = p.Expect(planes.planeCount == 3, "YUV planeCount: got %u want 3",
                              planes.planeCount);
    if (layout_ok) {
      const AHardwareBuffer_Plane& y = planes.planes[0];
      p.Expect(y.data != nullptr && y.pixelStride == 1 && y.rowStride >= kW,
               "Y plane: data=%p pixelStride=%u rowStride=%u", y.data, y.pixelStride, y.rowStride);
      for (int i = 1; i <= 2; ++i) {
        const AHardwareBuffer_Plane& c = planes.planes[i];
        // Chroma may be planar (pixelStride 1) or semi-planar interleaved
        // (pixelStride 2); the last sample of a row must still fit.
        const bool ok = c.data != nullptr && (c.pixelStride == 1 || c.pixelStride == 2) &&
                        c.rowStride >= (cw - 1) * c.pixelStride + 1;
        p.Expect(ok, "%s plane: data=%p pixelStride=%u rowStride=%u", i == 1 ? "Cb" : "Cr",
                 c.data, c.pixelStride, c.rowStride);
        layout_ok = layout_ok && ok;
      }
      layout_ok = layout_ok && y.data != nullptr && y.pixelStride == 1 && y.rowStride >= kW;
    }
    if (layout_ok) {
      FillPlane(planes.planes[0], kW, kH, PatternY);
      FillPlane(planes.planes[1], cw, ch, PatternCb);
      FillPlane(planes.planes[2], cw, ch, PatternCr);
    }
    int32_t fence = -1;
    ret = AHardwareBuffer_unlock(buf, &fence);
    if (fence >= 0) close(fence);
    p.Expect(ret == 0, "unlock after YUV write: ret=%d", ret);

    if (layout_ok) {
      AHardwareBuffer_Planes rplanes = {};
      ret = AHardwareBuffer_lockPlanes(buf, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, -1, nullptr,
                                       &rplanes);
      if (p.Expect(ret == 0 && rplanes.planeCount == 3,
                   "lockPlanes(YUV, READ): ret=%d planeCount=%u", ret, rplanes.planeCount)) {
        std::string err = VerifyPlane(rplanes.planes[0], kW, kH, PatternY);
        p.Expect(err.empty(), "Y plane readback: %s", err.c_str());
        err = VerifyPlane(rplanes.planes[1], cw, ch, PatternCb);
        p.Expect(err.empty(), "Cb plane readback: %s", err.c_str());
        err = VerifyPlane(rplanes.planes[2], cw, ch, PatternCr);
        p.Expect(err.empty(), "Cr plane readback: %s", err.c_str());
        fence = -1;
        ret = AHardwareBuffer_unlock(buf, &fence);
        if (fence >= 0) close(fence);
        p.Expect(ret == 0, "unlock after YUV read: ret=%d", ret);
      }
    }
    AHardwareBuffer_release(buf);
  } else {
    p.Skip("YUV lockPlanes: needs API 29, device is %d", android_get_device_api_level());
  }
}

// Handle transport over a socketpair between two threads. Socket timeouts
// bound the blocking calls; the future wait bounds the threads, so a wedged
// proxy call becomes a timed FAIL instead of a hang.
void SectionSocketTransport(Probe& p) {
  int fds[2];
  if (!p.Expect(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds) == 0, "socketpair: errno=%d",
                errno)) {
    return;
  }
  timeval tv = {};
  tv.tv_sec = 3;
  for (int fd : {fds[0], fds[1]}) {
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
  }

  std::atomic<uint64_t> sender_id{0};

  auto sender = [fd = fds[0], &sender_id]() -> std::string {
    AHardwareBuffer_Desc d = {};
    d.width = kW;
    d.height = kH;
    d.layers = 1;
    d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    d.usage = kCpuRw;
    AHardwareBuffer* buf = nullptr;
    int ret = AHardwareBuffer_allocate(&d, &buf);
    if (ret != 0 || buf == nullptr) return "allocate ret=" + std::to_string(ret);
    std::string err = LockWriteRgba(buf, kW, kH, PatternA);
    if (err.empty()) {
      if (__builtin_available(android 31, *)) {
        uint64_t id = 0;
        if (AHardwareBuffer_getId(buf, &id) == 0) sender_id.store(id);
      }
      ret = AHardwareBuffer_sendHandleToUnixSocket(buf, fd);
      if (ret != 0) err = "sendHandleToUnixSocket ret=" + std::to_string(ret);
    }
    AHardwareBuffer_release(buf);
    return err;
  };

  auto receiver = [fd = fds[1], &sender_id]() -> std::string {
    AHardwareBuffer* buf = nullptr;
    int ret = AHardwareBuffer_recvHandleFromUnixSocket(fd, &buf);
    if (ret != 0 || buf == nullptr) return "recvHandleFromUnixSocket ret=" + std::to_string(ret);
    std::string err;
    AHardwareBuffer_Desc d = {};
    AHardwareBuffer_describe(buf, &d);
    if (d.width != kW || d.height != kH || d.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM) {
      char m[96];
      snprintf(m, sizeof(m), "received desc %ux%u format=%u", d.width, d.height, d.format);
      err = m;
    } else {
      err = VerifyRgba(buf, kW, kH, PatternA);
      if (err.empty()) {
        if (__builtin_available(android 31, *)) {
          const uint64_t want = sender_id.load();
          uint64_t id = 0;
          if (AHardwareBuffer_getId(buf, &id) != 0) {
            err = "getId on received buffer failed";
          } else if (want != 0 && id != want) {
            char m[96];
            snprintf(m, sizeof(m), "received id 0x%" PRIx64 " != sent id 0x%" PRIx64, id, want);
            err = m;
          }
        }
      }
    }
    AHardwareBuffer_release(buf);
    return err;
  };

  std::packaged_task<std::string()> send_task(sender);
  std::packaged_task<std::string()> recv_task(receiver);
  std::future<std::string> send_result = send_task.get_future();
  std::future<std::string> recv_result = recv_task.get_future();
  std::thread send_thread(std::move(send_task));
  std::thread recv_thread(std::move(recv_task));

  bool all_joined = true;
  auto reap = [&](std::thread& t, std::future<std::string>& f, const char* what) {
    if (f.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
      t.join();
      const std::string err = f.get();
      p.Expect(err.empty(), "socket %s: %s", what, err.c_str());
    } else {
      t.detach();  // wedged in a proxy call; leaking beats hanging the probe
      all_joined = false;
      p.Fail("socket %s: timed out after 5s", what);
    }
  };
  reap(send_thread, send_result, "sender");
  reap(recv_thread, recv_result, "receiver");
  if (all_joined) {
    close(fds[0]);
    close(fds[1]);
  }
}

// AHardwareBuffer -> Java HardwareBuffer -> AHardwareBuffer, all in native.
void SectionJniRoundTrip(JNIEnv* env, Probe& p) {
  constexpr uint32_t w = 32, h = 16;
  AHardwareBuffer_Desc d = {};
  d.width = w;
  d.height = h;
  d.layers = 1;
  d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  d.usage = kCpuRw;
  AHardwareBuffer* buf = nullptr;
  int ret = AHardwareBuffer_allocate(&d, &buf);
  if (!p.Expect(ret == 0 && buf != nullptr, "allocate for jni round-trip: ret=%d", ret)) return;
  std::string err = LockWriteRgba(buf, w, h, PatternB);
  p.Expect(err.empty(), "jni round-trip fill: %s", err.c_str());

  jobject jhb = AHardwareBuffer_toHardwareBuffer(env, buf);
  if (p.Expect(jhb != nullptr, "toHardwareBuffer: returned null")) {
    AHardwareBuffer* back = AHardwareBuffer_fromHardwareBuffer(env, jhb);
    if (p.Expect(back != nullptr, "fromHardwareBuffer: returned null")) {
      // The Java object wraps the same native buffer; a proxy that fabricates
      // a new handle here breaks zero-copy identity.
      p.Expect(back == buf, "from(to(buf)): got %p want %p", static_cast<void*>(back),
               static_cast<void*>(buf));
      AHardwareBuffer_Desc a = {}, b = {};
      AHardwareBuffer_describe(buf, &a);
      AHardwareBuffer_describe(back, &b);
      p.Expect(a.width == b.width && a.height == b.height && a.layers == b.layers &&
                   a.format == b.format && a.usage == b.usage && a.stride == b.stride,
               "describe mismatch after round-trip: %ux%u/%u vs %ux%u/%u", a.width, a.height,
               a.format, b.width, b.height, b.format);
      if (__builtin_available(android 31, *)) {
        uint64_t ia = 0, ib = 0;
        const bool ok =
            AHardwareBuffer_getId(buf, &ia) == 0 && AHardwareBuffer_getId(back, &ib) == 0;
        p.Expect(ok && ia == ib, "getId after round-trip: 0x%" PRIx64 " vs 0x%" PRIx64, ia, ib);
      }
      err = VerifyRgba(back, w, h, PatternB);
      p.Expect(err.empty(), "pixels via round-tripped handle: %s", err.c_str());
    }
    env->DeleteLocalRef(jhb);
  }
  AHardwareBuffer_release(buf);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellohardwarebuffer_MainActivity_probeHardwareBuffer(JNIEnv* env,
                                                                      jobject /*this*/) {
  Probe p;
  SectionBasics(p);
  SectionYuvPlanes(p);
  SectionSocketTransport(p);
  SectionJniRoundTrip(env, p);
  return env->NewStringUTF(p.Summary("hellohardwarebuffer").c_str());
}

// Allocates kJavaW x kJavaH RGBA, fills PatternB, returns it wrapped as a Java
// HardwareBuffer. Kotlin checks the SDK getters and passes it back to
// verifyTestBuffer below.
extern "C" JNIEXPORT jobject JNICALL
Java_com_example_hellohardwarebuffer_MainActivity_createTestBuffer(JNIEnv* env, jobject /*this*/) {
  AHardwareBuffer_Desc d = {};
  d.width = kJavaW;
  d.height = kJavaH;
  d.layers = 1;
  d.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  d.usage = kCpuRw;
  AHardwareBuffer* buf = nullptr;
  int ret = AHardwareBuffer_allocate(&d, &buf);
  if (ret != 0 || buf == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "FAIL at createTestBuffer allocate: ret=%d",
                        ret);
    return nullptr;
  }
  const std::string err = LockWriteRgba(buf, kJavaW, kJavaH, PatternB);
  if (!err.empty()) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "FAIL at createTestBuffer fill: %s",
                        err.c_str());
  }
  jobject jhb = AHardwareBuffer_toHardwareBuffer(env, buf);
  if (jhb == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "FAIL at createTestBuffer toHardwareBuffer: returned null");
  }
  // The Java HardwareBuffer holds its own reference now.
  AHardwareBuffer_release(buf);
  return jhb;
}

// Receives the Java HardwareBuffer created above after a trip through Kotlin,
// unwraps it, and verifies description and every pixel.
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellohardwarebuffer_MainActivity_verifyTestBuffer(JNIEnv* env, jobject /*this*/,
                                                                   jobject jbuffer) {
  Probe p;
  AHardwareBuffer* buf = AHardwareBuffer_fromHardwareBuffer(env, jbuffer);
  if (p.Expect(buf != nullptr, "fromHardwareBuffer(java-passed): returned null")) {
    // fromHardwareBuffer does not acquire; take a reference across the checks.
    AHardwareBuffer_acquire(buf);
    AHardwareBuffer_Desc d = {};
    AHardwareBuffer_describe(buf, &d);
    p.Expect(d.width == kJavaW && d.height == kJavaH && d.layers == 1 &&
                 d.format == AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM,
             "java-passed describe: %ux%u layers=%u format=%u", d.width, d.height, d.layers,
             d.format);
    const std::string err = VerifyRgba(buf, kJavaW, kJavaH, PatternB);
    p.Expect(err.empty(), "java-passed pixels: %s", err.c_str());
    AHardwareBuffer_release(buf);
  }
  return env->NewStringUTF(p.Summary("java-interop").c_str());
}
