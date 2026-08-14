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

#include <android/choreographer.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/performance_hint.h>
#include <android/thermal.h>
#include <jni.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#define LOG_TAG "helloadpf"

namespace {

int64_t NowNanos() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int64_t AbsDiff(int64_t a, int64_t b) {
  int64_t d = a - b;
  return d < 0 ? -d : d;
}

// Collects one line per check; a failing check logs and records "FAIL at ...",
// which trips StatusTestRule's marker scan.
struct Reporter {
  std::string text;
  bool failed = false;

  __attribute__((format(printf, 2, 3))) void Note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Add(false, fmt, ap);
    va_end(ap);
  }

  __attribute__((format(printf, 2, 3))) void Fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    Add(true, fmt, ap);
    va_end(ap);
  }

 private:
  void Add(bool is_fail, const char* fmt, va_list ap) {
    char line[512];
    vsnprintf(line, sizeof(line), fmt, ap);
    if (is_fail) {
      failed = true;
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "FAIL at %s", line);
      text += "FAIL at ";
    } else {
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", line);
    }
    text += line;
    text += "\n";
  }
};

// Measurable fake per-frame work: FNV-1a over a counter stream. The result is
// accumulated and logged so the loop cannot be optimized away.
uint64_t FakeWork(int seed) {
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < 300000; ++i) {
    h ^= static_cast<uint64_t>(i + seed);
    h *= 1099511628211ULL;
  }
  return h;
}

// --- APerformanceHint (API 33+; 34/35 members behind availability guards) ----

void ProbePerformanceHint(Reporter& r) {
  APerformanceHintManager* mgr = APerformanceHint_getManager();
  if (mgr == nullptr) {
    // Legitimate on an emulator with no hint service: SKIP, not a failure.
    r.Note("hint: SKIP (APerformanceHint_getManager returned null)");
    return;
  }
  int64_t rate = APerformanceHint_getPreferredUpdateRateNanos(mgr);
  r.Note("hint: preferredUpdateRateNanos=%" PRId64, rate);

  const int32_t tid = static_cast<int32_t>(gettid());
  constexpr int64_t kTargetNanos = 16666667;  // one 60 Hz frame
  APerformanceHintSession* session =
      APerformanceHint_createSession(mgr, &tid, 1, kTargetNanos);
  if (session == nullptr) {
    r.Note("hint: SKIP (createSession returned null)");
    return;
  }

  // A session WAS returned: from here every int-returning call must be 0.
  int rc = APerformanceHint_updateTargetWorkDuration(session, kTargetNanos);
  if (rc != 0) r.Fail("hint updateTargetWorkDuration rc=%d", rc);

  uint64_t sink = 0;
  int frames_ok = 0;
  for (int frame = 0; frame < 20; ++frame) {
    const int64_t start = NowNanos();
    sink ^= FakeWork(frame);
    int64_t actual = NowNanos() - start;
    if (actual <= 0) actual = 1;  // reportActualWorkDuration requires > 0
    rc = APerformanceHint_reportActualWorkDuration(session, actual);
    if (rc != 0) {
      r.Fail("hint reportActualWorkDuration frame=%d rc=%d", frame, rc);
      break;
    }
    ++frames_ok;
  }
  if (frames_ok == 20)
    r.Note("hint: reported 20 frames (work sink=%04x)",
           static_cast<unsigned>(sink & 0xffff));

  if (__builtin_available(android 34, *)) {
    pid_t tids[1] = {gettid()};
    rc = APerformanceHint_setThreads(session, tids, 1);
    if (rc != 0) r.Fail("hint setThreads rc=%d", rc);
    else r.Note("hint: setThreads OK");
  }

  if (__builtin_available(android 35, *)) {
    rc = APerformanceHint_setPreferPowerEfficiency(session, true);
    if (rc != 0) r.Fail("hint setPreferPowerEfficiency rc=%d", rc);

    AWorkDuration* wd = AWorkDuration_create();
    if (wd == nullptr) {
      r.Fail("hint AWorkDuration_create returned null");
    } else {
      // Contract: period start > 0, total > 0, cpu/gpu >= 0, one of them > 0.
      AWorkDuration_setWorkPeriodStartTimestampNanos(wd, NowNanos() - 2000000);
      AWorkDuration_setActualTotalDurationNanos(wd, 2000000);
      AWorkDuration_setActualCpuDurationNanos(wd, 1500000);
      AWorkDuration_setActualGpuDurationNanos(wd, 0);
      rc = APerformanceHint_reportActualWorkDuration2(session, wd);
      if (rc != 0) r.Fail("hint reportActualWorkDuration2 rc=%d", rc);
      else r.Note("hint: reportActualWorkDuration2 OK");
      AWorkDuration_release(wd);
    }
  }

  APerformanceHint_closeSession(session);
  r.Note("hint: session closed");
}

// --- AThermal (API 30+; thresholds API 35 behind guard) ----------------------

std::atomic<int> g_thermal_cb_count{0};

// Host-calls-guest: the thermal service invokes this on a binder thread.
void ThermalStatusCallback(void* /*data*/, AThermalStatus status) {
  g_thermal_cb_count.fetch_add(1);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "thermal listener fired: status=%d",
                      static_cast<int>(status));
}

void ProbeThermal(Reporter& r) {
  AThermalManager* mgr = AThermal_acquireManager();
  if (mgr == nullptr) {
    r.Note("thermal: SKIP (acquireManager returned null)");
    return;
  }

  const AThermalStatus status = AThermal_getCurrentThermalStatus(mgr);
  if (status < ATHERMAL_STATUS_NONE || status > ATHERMAL_STATUS_SHUTDOWN)
    r.Fail("thermal getCurrentThermalStatus=%d (ERROR or out of range)",
           static_cast<int>(status));
  else
    r.Note("thermal: status=%d", static_cast<int>(status));

  // NaN is legal (emulator / unsupported); a negative value is not — the API
  // clamps negative headroom to 0.0 before returning.
  const float h0 = AThermal_getThermalHeadroom(mgr, 0);
  const float h10 = AThermal_getThermalHeadroom(mgr, 10);
  if (!std::isnan(h0) && h0 < 0.0f) r.Fail("thermal headroom(0)=%f negative", h0);
  if (!std::isnan(h10) && h10 < 0.0f) r.Fail("thermal headroom(10)=%f negative", h10);
  r.Note("thermal: headroom(0)=%f headroom(10)=%f", h0, h10);

  int rc = AThermal_registerThermalStatusListener(mgr, ThermalStatusCallback,
                                                  &g_thermal_cb_count);
  if (rc != 0) {
    r.Fail("thermal registerThermalStatusListener rc=%d", rc);
  } else {
    // The listener will likely never fire on an emulator; the assertion is
    // that registration succeeded and the process survives 500 ms with a
    // guest callback registered against the host thermal service.
    usleep(500 * 1000);
    rc = AThermal_unregisterThermalStatusListener(mgr, ThermalStatusCallback,
                                                  &g_thermal_cb_count);
    if (rc != 0)
      r.Fail("thermal unregisterThermalStatusListener rc=%d", rc);
    else
      r.Note("thermal: listener register/500ms/unregister OK (fired %d times)",
             g_thermal_cb_count.load());
  }

  if (__builtin_available(android 35, *)) {
    const AThermalHeadroomThreshold* thresholds = nullptr;
    size_t count = 0;
    rc = AThermal_getThermalHeadroomThresholds(mgr, &thresholds, &count);
    if (rc == 0) {
      if (count > 0 && thresholds == nullptr)
        r.Fail("thermal thresholds count=%zu but array is null", count);
      else
        r.Note("thermal: %zu headroom thresholds", count);
    } else if (rc == ENOSYS || rc == EPIPE) {
      r.Note("thermal: SKIP thresholds (rc=%d, unsupported here)", rc);
    } else {
      r.Fail("thermal getThermalHeadroomThresholds rc=%d", rc);
    }
  }

  AThermal_releaseManager(mgr);
  r.Note("thermal: manager released");
}

// --- AChoreographer (API 24+/29+/30+/33+) ------------------------------------
//
// All callbacks below are guest function pointers invoked by host UI machinery
// on the looper thread we create — the riskiest path in this probe. State is
// static storage so a late callback can never touch freed memory.

struct ChoreoState {
  std::mutex m;
  std::condition_variable cv;
  AChoreographer* choreo = nullptr;
  int64_t post_time = 0;
  bool looper_ok = false;
  bool instance_ok = false;
  bool frame1 = false, frame2 = false, refresh = false, vsync = false;
  int64_t frame1_time = 0, frame1_latency = 0;
  int64_t frame2_time = 0;
  int64_t refresh_period = 0;
  int64_t vsync_frame_time = 0, vsync_expected = 0, vsync_deadline = 0;
  int64_t vsync_id = -1;
  size_t vsync_timelines = 0, vsync_preferred = 0;
  bool done = false;
};

ChoreoState g_choreo;

void SecondFrameCallback(int64_t frameTimeNanos, void* data) {
  auto* st = static_cast<ChoreoState*>(data);
  std::lock_guard<std::mutex> lk(st->m);
  st->frame2 = true;
  st->frame2_time = frameTimeNanos;
}

void FirstFrameCallback(int64_t frameTimeNanos, void* data) {
  auto* st = static_cast<ChoreoState*>(data);
  {
    std::lock_guard<std::mutex> lk(st->m);
    st->frame1 = true;
    st->frame1_time = frameTimeNanos;
    st->frame1_latency = NowNanos() - st->post_time;
  }
  // Chain a second callback from inside the first.
  AChoreographer_postFrameCallback64(st->choreo, SecondFrameCallback, st);
}

void RefreshRateCallback(int64_t vsyncPeriodNanos, void* data) {
  auto* st = static_cast<ChoreoState*>(data);
  std::lock_guard<std::mutex> lk(st->m);
  st->refresh = true;
  st->refresh_period = vsyncPeriodNanos;
}

void VsyncCallback(const AChoreographerFrameCallbackData* cbData, void* data) {
  auto* st = static_cast<ChoreoState*>(data);
  // cbData is a host-owned opaque pointer handed back into these getters; it
  // does not outlive the callback.
  const size_t len = AChoreographerFrameCallbackData_getFrameTimelinesLength(cbData);
  const size_t pref =
      AChoreographerFrameCallbackData_getPreferredFrameTimelineIndex(cbData);
  const int64_t ft = AChoreographerFrameCallbackData_getFrameTimeNanos(cbData);
  int64_t vid = -1, expected = 0, deadline = 0;
  if (len > 0) {
    const size_t idx = pref < len ? pref : 0;
    vid = AChoreographerFrameCallbackData_getFrameTimelineVsyncId(cbData, idx);
    expected =
        AChoreographerFrameCallbackData_getFrameTimelineExpectedPresentationTimeNanos(
            cbData, idx);
    deadline =
        AChoreographerFrameCallbackData_getFrameTimelineDeadlineNanos(cbData, idx);
  }
  std::lock_guard<std::mutex> lk(st->m);
  st->vsync = true;
  st->vsync_timelines = len;
  st->vsync_preferred = pref;
  st->vsync_frame_time = ft;
  st->vsync_id = vid;
  st->vsync_expected = expected;
  st->vsync_deadline = deadline;
}

void ChoreoThreadMain() {
  ChoreoState* st = &g_choreo;
  ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
  AChoreographer* choreo = nullptr;
  {
    std::lock_guard<std::mutex> lk(st->m);
    st->looper_ok = looper != nullptr;
  }
  if (looper != nullptr) {
    choreo = AChoreographer_getInstance();
    std::lock_guard<std::mutex> lk(st->m);
    st->instance_ok = choreo != nullptr;
    st->choreo = choreo;
  }
  if (choreo != nullptr) {
    {
      std::lock_guard<std::mutex> lk(st->m);
      st->post_time = NowNanos();
    }
    AChoreographer_postFrameCallback64(choreo, FirstFrameCallback, st);
    AChoreographer_registerRefreshRateCallback(choreo, RefreshRateCallback, st);
    AChoreographer_postVsyncCallback(choreo, VsyncCallback, st);

    const int64_t deadline = NowNanos() + 4000000000LL;  // 4 s pump window
    while (NowNanos() < deadline) {
      {
        std::lock_guard<std::mutex> lk(st->m);
        if (st->frame1 && st->frame2 && st->refresh && st->vsync) break;
      }
      int fd = 0, events = 0;
      void* pollData = nullptr;
      ALooper_pollOnce(100, &fd, &events, &pollData);
    }
    AChoreographer_unregisterRefreshRateCallback(choreo, RefreshRateCallback, st);
  }
  {
    std::lock_guard<std::mutex> lk(st->m);
    st->done = true;
  }
  st->cv.notify_all();
}

void ProbeChoreographer(Reporter& r) {
  ChoreoState* st = &g_choreo;
  std::thread worker(ChoreoThreadMain);

  bool finished;
  {
    std::unique_lock<std::mutex> lk(st->m);
    finished =
        st->cv.wait_for(lk, std::chrono::seconds(6), [st] { return st->done; });
  }
  if (!finished) {
    r.Fail("choreographer worker wedged (no completion within 6s)");
    worker.detach();  // g_choreo is static storage; nothing dangles
    return;
  }
  worker.join();

  std::lock_guard<std::mutex> lk(st->m);
  if (!st->looper_ok) {
    r.Fail("choreographer ALooper_prepare returned null");
    return;
  }
  if (!st->instance_ok) {
    r.Fail("choreographer AChoreographer_getInstance returned null");
    return;
  }

  const int64_t now = NowNanos();
  if (!st->frame1) {
    r.Fail("choreographer postFrameCallback64 never fired");
  } else {
    if (st->frame1_latency > 2000000000LL)
      r.Fail("choreographer frame callback latency %" PRId64 " ns > 2s",
             st->frame1_latency);
    if (AbsDiff(st->frame1_time, now) > 5000000000LL)
      r.Fail("choreographer frameTimeNanos=%" PRId64 " implausible vs now=%" PRId64,
             st->frame1_time, now);
    if (!st->frame2)
      r.Fail("choreographer chained frame callback never fired");
    else if (st->frame2_time < st->frame1_time)
      r.Fail("choreographer chained frame time %" PRId64 " < first %" PRId64,
             st->frame2_time, st->frame1_time);
    else
      r.Note("choreographer: frame callbacks fired (latency=%" PRId64
             " ns, chained OK)",
             st->frame1_latency);
  }

  if (!st->refresh) {
    // Fires immediately on registration on real devices; emulator behavior may
    // differ, so a silent registration is a SKIP. Register+unregister not
    // crashing is the hard assertion.
    r.Note("choreographer: SKIP refresh-rate callback (did not fire; "
           "register/unregister completed)");
  } else if (st->refresh_period < 1000000LL || st->refresh_period > 100000000LL) {
    r.Fail("choreographer refresh period %" PRId64 " ns outside [1ms,100ms]",
           st->refresh_period);
  } else {
    r.Note("choreographer: refresh period %" PRId64 " ns", st->refresh_period);
  }

  if (!st->vsync) {
    r.Fail("choreographer postVsyncCallback never fired");
  } else {
    if (st->vsync_timelines < 1)
      r.Fail("choreographer vsync data has %zu frame timelines",
             st->vsync_timelines);
    else if (st->vsync_preferred >= st->vsync_timelines)
      r.Fail("choreographer vsync preferred index %zu out of range (len=%zu)",
             st->vsync_preferred, st->vsync_timelines);
    else if (st->vsync_expected <= 0 || st->vsync_deadline <= 0)
      r.Fail("choreographer vsync timeline times invalid (expected=%" PRId64
             " deadline=%" PRId64 ")",
             st->vsync_expected, st->vsync_deadline);
    else if (AbsDiff(st->vsync_frame_time, now) > 5000000000LL)
      r.Fail("choreographer vsync frameTimeNanos=%" PRId64 " implausible",
             st->vsync_frame_time);
    else
      r.Note("choreographer: vsync data OK (timelines=%zu preferred=%zu "
             "vsyncId=%" PRId64 ")",
             st->vsync_timelines, st->vsync_preferred, st->vsync_id);
  }
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloadpf_MainActivity_probeAdpf(JNIEnv* env, jobject /*this*/) {
  Reporter r;
  ProbePerformanceHint(r);
  ProbeThermal(r);
  ProbeChoreographer(r);

  std::string msg = r.failed ? r.text : "helloadpf OK:\n" + r.text;
  __android_log_print(r.failed ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, LOG_TAG,
                      "%s", r.failed ? "probe finished with failures"
                                     : "probe finished cleanly");
  return env->NewStringUTF(msg.c_str());
}
