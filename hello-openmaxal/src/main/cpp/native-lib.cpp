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

#include <OMXAL/OpenMAXAL.h>
#include <OMXAL/OpenMAXAL_Android.h>
#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#define LOG_TAG "helloopenmaxal"

namespace {

// XA interfaces are pointers to structs of function pointers; every
// (*itf)->Method(itf, ...) below is a through-the-vtable call whose target the
// proxy must have made guest-callable. That, not media output, is what this
// probe establishes.

// Formats the UUID an XAInterfaceID points at. Reading through the pointer is
// deliberate: garbage out-marshalling of an IID crashes or prints nonsense
// right here.
std::string FormatIid(XAInterfaceID iid) {
  if (iid == nullptr) return "<null>";
  char buf[48];
  snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%02x%02x%02x%02x%02x%02x",
           iid->time_low, iid->time_mid, iid->time_hi_and_version,
           iid->clock_seq, iid->node[0], iid->node[1], iid->node[2],
           iid->node[3], iid->node[4], iid->node[5]);
  return buf;
}

// IIDs compare by UUID content, never by pointer: the implementation may hand
// back a different pointer than the exported XA_IID_* data symbol.
bool SameIid(XAInterfaceID a, XAInterfaceID b) {
  return a != nullptr && b != nullptr &&
         memcmp(a, b, sizeof(struct XAInterfaceID_)) == 0;
}

// Host-calls-guest path: the implementation invokes this guest function
// pointer to deliver object events (async Realize completion).
struct CallbackWaiter {
  std::mutex m;
  std::condition_variable cv;
  bool fired = false;
  XAresult result = XA_RESULT_UNKNOWN_ERROR;
};

void XAAPIENTRY ObjectCallback(XAObjectItf /*caller*/, const void* pContext,
                               XAuint32 event, XAresult result,
                               XAuint32 /*param*/, void* /*pInterface*/) {
  auto* w = static_cast<CallbackWaiter*>(const_cast<void*>(pContext));
  if (w == nullptr) return;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "object callback: event=%u result=%u", event, result);
  if (event == XA_OBJECT_EVENT_ASYNC_TERMINATION) {
    std::lock_guard<std::mutex> lock(w->m);
    w->fired = true;
    w->result = result;
    w->cv.notify_all();
  }
}

// Two more host-calls-guest paths, on the media player's interfaces. Neither is
// expected to fire here — nothing is ever prefetched or played — so they only
// count deliveries, and the counts are reported rather than asserted. The
// signatures must match xaDynamicInterfaceManagementCallback and
// xaPrefetchCallback exactly, including the trailing XAInterfaceID the former
// takes: a mis-marshalled parameter list corrupts the stack on delivery.
struct CallbackCounter {
  std::atomic<int> count{0};
};

void XAAPIENTRY DynamicInterfaceCallback(
    XADynamicInterfaceManagementItf /*caller*/, void* pContext,
    XAuint32 /*event*/, XAresult /*result*/, const XAInterfaceID /*iid*/) {
  auto* c = static_cast<CallbackCounter*>(pContext);
  if (c == nullptr) return;
  c->count.fetch_add(1, std::memory_order_relaxed);
}

void XAAPIENTRY PrefetchCallback(XAPrefetchStatusItf /*caller*/, void* pContext,
                                 XAuint32 /*event*/) {
  auto* c = static_cast<CallbackCounter*>(pContext);
  if (c == nullptr) return;
  c->count.fetch_add(1, std::memory_order_relaxed);
}

jstring Fail(JNIEnv* env, const char* tag, XAuint32 result, XAuint32 extra = 0) {
  char m[192];
  snprintf(m, sizeof(m), "helloopenmaxal FAIL at %s: result=0x%08x extra=0x%08x",
           tag, result, extra);
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", m);
  return env->NewStringUTF(m);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloopenmaxal_MainActivity_probeOpenMAXAL(JNIEnv* env,
                                                            jobject /*this*/) {
  // --- 1. Engine creation (the API's only global constructor) ---------------
  XAObjectItf engineObj = nullptr;
  XAresult res = xaCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr);
  if (res != XA_RESULT_SUCCESS || engineObj == nullptr) {
    return Fail(env, "xaCreateEngine", res);
  }

  // --- 2. First vtable call: is (*engineObj) a callable table? --------------
  res = (*engineObj)->Realize(engineObj, XA_BOOLEAN_FALSE);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "engine Realize", res);
  XAuint32 state = 0;
  res = (*engineObj)->GetState(engineObj, &state);
  if (res != XA_RESULT_SUCCESS || state != XA_OBJECT_STATE_REALIZED) {
    return Fail(env, "engine GetState", res, state);
  }

  // --- 3. XAEngineItf + per-class supported-interface enumeration -----------
  XAEngineItf engineItf = nullptr;
  res = (*engineObj)->GetInterface(engineObj, XA_IID_ENGINE, &engineItf);
  if (res != XA_RESULT_SUCCESS || engineItf == nullptr) {
    return Fail(env, "GetInterface(XA_IID_ENGINE)", res);
  }

  const struct {
    const char* name;
    XAuint32 id;
  } kClasses[] = {
      {"engine", XA_OBJECTID_ENGINE},
      {"outputmix", XA_OBJECTID_OUTPUTMIX},
      {"mediaplayer", XA_OBJECTID_MEDIAPLAYER},
  };
  std::string classCounts;
  for (const auto& c : kClasses) {
    XAuint32 n = 0;
    res = (*engineItf)->QueryNumSupportedInterfaces(engineItf, c.id, &n);
    if (res == XA_RESULT_FEATURE_UNSUPPORTED && c.id != XA_OBJECTID_ENGINE) {
      // Only the engine class is mandated; other classes may be absent.
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                          "SKIP: class %s reports feature-unsupported", c.name);
      classCounts += std::string(" ") + c.name + "=skip";
      continue;
    }
    if (res != XA_RESULT_SUCCESS) {
      return Fail(env, "QueryNumSupportedInterfaces", res, c.id);
    }
    if (c.id == XA_OBJECTID_ENGINE && n == 0) {
      return Fail(env, "engine class interface count", 0, n);
    }
    bool engineIidSeen = false;
    for (XAuint32 i = 0; i < n; ++i) {
      XAInterfaceID iid = nullptr;
      res = (*engineItf)->QuerySupportedInterfaces(engineItf, c.id, i, &iid);
      if (res != XA_RESULT_SUCCESS || iid == nullptr) {
        return Fail(env, "QuerySupportedInterfaces", res, i);
      }
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s itf[%u] = %s", c.name,
                          i, FormatIid(iid).c_str());
      if (SameIid(iid, XA_IID_ENGINE)) engineIidSeen = true;
    }
    // XAEngineItf is a mandated interface of the engine class; its IID must
    // enumerate with intact UUID content.
    if (c.id == XA_OBJECTID_ENGINE && !engineIidSeen) {
      return Fail(env, "engine class missing XA_IID_ENGINE content", n);
    }
    classCounts += std::string(" ") + c.name + "=" + std::to_string(n);
  }

  // --- 4. Global engine-interface query functions ---------------------------
  XAuint32 nGlobal = 0;
  res = xaQueryNumSupportedEngineInterfaces(&nGlobal);
  if (res != XA_RESULT_SUCCESS || nGlobal == 0) {
    return Fail(env, "xaQueryNumSupportedEngineInterfaces", res, nGlobal);
  }
  bool globalEngineIidSeen = false;
  for (XAuint32 i = 0; i < nGlobal; ++i) {
    XAInterfaceID iid = nullptr;
    res = xaQuerySupportedEngineInterfaces(i, &iid);
    if (res != XA_RESULT_SUCCESS || iid == nullptr) {
      return Fail(env, "xaQuerySupportedEngineInterfaces", res, i);
    }
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "global engine itf[%u] = %s",
                        i, FormatIid(iid).c_str());
    if (SameIid(iid, XA_IID_ENGINE)) globalEngineIidSeen = true;
  }
  if (!globalEngineIidSeen) {
    return Fail(env, "global list missing XA_IID_ENGINE content", nGlobal);
  }

  // --- 5. Output mix: create through the engine vtable, sync lifecycle ------
  XAObjectItf mixObj = nullptr;
  res = (*engineItf)->CreateOutputMix(engineItf, &mixObj, 0, nullptr, nullptr);
  if (res != XA_RESULT_SUCCESS || mixObj == nullptr) {
    return Fail(env, "CreateOutputMix", res);
  }
  res = (*mixObj)->Realize(mixObj, XA_BOOLEAN_FALSE);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "outputmix Realize", res);
  state = 0;
  res = (*mixObj)->GetState(mixObj, &state);
  if (res != XA_RESULT_SUCCESS || state != XA_OBJECT_STATE_REALIZED) {
    return Fail(env, "outputmix GetState", res, state);
  }
  std::string volNote;
  XAVolumeItf volItf = nullptr;
  res = (*mixObj)->GetInterface(mixObj, XA_IID_VOLUME, &volItf);
  if (res == XA_RESULT_SUCCESS && volItf != nullptr) {
    XAmillibel level = 0;
    res = (*volItf)->GetVolumeLevel(volItf, &level);
    if (res != XA_RESULT_SUCCESS) return Fail(env, "GetVolumeLevel", res);
    volNote = "volume level=" + std::to_string(static_cast<int>(level));
  } else if (res == XA_RESULT_FEATURE_UNSUPPORTED) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "SKIP: outputmix has no XA_IID_VOLUME");
    volNote = "volume skip (feature-unsupported)";
  } else {
    return Fail(env, "GetInterface(XA_IID_VOLUME) on outputmix", res);
  }
  (*mixObj)->Destroy(mixObj);

  // --- 6. Lifecycle robustness on the engine object -------------------------
  // XA_IID_PLAY belongs to the media player class; the engine must reject it
  // with an error code, never crash and never succeed.
  std::string playNote;
  void* playRaw = nullptr;
  res = (*engineObj)->GetInterface(engineObj, XA_IID_PLAY, &playRaw);
  if (res == XA_RESULT_FEATURE_UNSUPPORTED) {
    playNote = "play-on-engine rejected feature-unsupported";
  } else if (res == XA_RESULT_PARAMETER_INVALID) {
    playNote = "play-on-engine rejected parameter-invalid";
  } else {
    return Fail(env, "GetInterface(XA_IID_PLAY) on engine expected rejection",
                res);
  }

  CallbackWaiter engineWaiter;
  res = (*engineObj)->RegisterCallback(engineObj, ObjectCallback, &engineWaiter);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "engine RegisterCallback", res);
  (*engineObj)->AbortAsyncOperation(engineObj);  // legal no-op; must not crash

  // --- 7. Async Realize: registration must succeed; servicing is optional ---
  // Android's wilhelm compiles its ThreadPool with THREAD_TYPICAL == 0 (no
  // worker threads unless USE_ASYNCHRONOUS_*_CALLBACK is set), so an async
  // Realize is enqueued and never serviced — identically for a native x86_64
  // build of this probe. The wait below therefore times out as a SKIP, not a
  // FAIL. On such a platform the object stays REALIZING forever, and both
  // AbortAsyncOperation and Destroy would spin in wilhelm's Abort_internal
  // poll loop — so the object is deliberately leaked instead of destroyed.
  static CallbackWaiter mixWaiter;  // static: outlives us if a callback lands late
  XAObjectItf mix2 = nullptr;
  res = (*engineItf)->CreateOutputMix(engineItf, &mix2, 0, nullptr, nullptr);
  if (res != XA_RESULT_SUCCESS || mix2 == nullptr) {
    return Fail(env, "CreateOutputMix (async probe)", res);
  }
  res = (*mix2)->RegisterCallback(mix2, ObjectCallback, &mixWaiter);
  if (res != XA_RESULT_SUCCESS) {
    return Fail(env, "outputmix RegisterCallback", res);
  }
  res = (*mix2)->Realize(mix2, XA_BOOLEAN_TRUE);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "outputmix async Realize", res);
  std::string asyncNote;
  bool asyncFired;
  {
    std::unique_lock<std::mutex> lock(mixWaiter.m);
    asyncFired = mixWaiter.cv.wait_for(lock, std::chrono::seconds(2),
                                       [&] { return mixWaiter.fired; });
  }
  if (asyncFired) {
    if (mixWaiter.result != XA_RESULT_SUCCESS) {
      return Fail(env, "async Realize completion result", mixWaiter.result);
    }
    state = 0;
    res = (*mix2)->GetState(mix2, &state);
    if (res != XA_RESULT_SUCCESS || state != XA_OBJECT_STATE_REALIZED) {
      return Fail(env, "outputmix GetState after async Realize", res, state);
    }
    (*mix2)->Destroy(mix2);
    asyncNote = "async realize callback ok";
  } else {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "SKIP: async realize never serviced (wilhelm ships no "
                        "ThreadPool workers on Android); leaking the mix object");
    asyncNote = "async realize skip (platform has no async workers)";
  }

  // --- 8. Media player: dynamic interfaces, seek, prefetch status -----------
  // A media player takes a realized output mix as its audio sink and holds a
  // strong reference to it, so this mix is created here and torn down after the
  // player. The URI source is never opened: nothing prefetches or plays, so the
  // path only has to be well-formed, not resolvable.
  XAObjectItf playerMixObj = nullptr;
  res = (*engineItf)->CreateOutputMix(engineItf, &playerMixObj, 0, nullptr,
                                      nullptr);
  if (res != XA_RESULT_SUCCESS || playerMixObj == nullptr) {
    return Fail(env, "CreateOutputMix (media player sink)", res);
  }
  res = (*playerMixObj)->Realize(playerMixObj, XA_BOOLEAN_FALSE);
  if (res != XA_RESULT_SUCCESS) {
    return Fail(env, "media player sink mix Realize", res);
  }

  XADataLocator_URI locUri = {
      XA_DATALOCATOR_URI,
      reinterpret_cast<XAchar*>(
          const_cast<char*>("file:///system/media/audio/ui/camera_click.ogg"))};
  XADataFormat_MIME fmtMime = {XA_DATAFORMAT_MIME, nullptr,
                               XA_CONTAINERTYPE_UNSPECIFIED};
  XADataSource dataSrc = {&locUri, &fmtMime};
  XADataLocator_OutputMix locOutMix = {XA_DATALOCATOR_OUTPUTMIX, playerMixObj};
  XADataSink audioSnk = {&locOutMix, nullptr};

  // XA_IID_SEEK and XA_IID_PREFETCHSTATUS are explicit interfaces of the media
  // player class: they exist only if asked for at creation. Marking them
  // required means an implementation that cannot expose them fails loudly here
  // instead of handing back a null interface later.
  // XA_IID_DYNAMICINTERFACEMANAGEMENT is deliberately absent from the list —
  // it is implicit, so every media player must already carry it.
  const XAInterfaceID playerIids[] = {XA_IID_SEEK, XA_IID_PREFETCHSTATUS};
  const XAboolean playerRequired[] = {XA_BOOLEAN_TRUE, XA_BOOLEAN_TRUE};
  XAObjectItf playerObj = nullptr;
  res = (*engineItf)->CreateMediaPlayer(
      engineItf, &playerObj, &dataSrc, nullptr, &audioSnk, nullptr, nullptr,
      nullptr, sizeof(playerIids) / sizeof(playerIids[0]), playerIids,
      playerRequired);
  if (res != XA_RESULT_SUCCESS || playerObj == nullptr) {
    return Fail(env, "CreateMediaPlayer", res);
  }
  res = (*playerObj)->Realize(playerObj, XA_BOOLEAN_FALSE);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "mediaplayer Realize", res);
  state = 0;
  res = (*playerObj)->GetState(playerObj, &state);
  if (res != XA_RESULT_SUCCESS || state != XA_OBJECT_STATE_REALIZED) {
    return Fail(env, "mediaplayer GetState", res, state);
  }

  // static: these outlive us if a callback lands late.
  static CallbackCounter dimCalls;
  static CallbackCounter prefetchCalls;

  // XADynamicInterfaceManagementItf is implicit in the media player's class
  // table, but the platform's interface-init table carries no init hook for its
  // MPH, so the object never exposes it and GetInterface reports
  // feature-unsupported — on real devices exactly as here. The assertion is
  // that the query is refused cleanly; should a platform ever expose it, the
  // registration is exercised and must succeed.
  std::string dimNote;
  XADynamicInterfaceManagementItf dimItf = nullptr;
  res = (*playerObj)->GetInterface(playerObj,
                                   XA_IID_DYNAMICINTERFACEMANAGEMENT, &dimItf);
  if (res == XA_RESULT_SUCCESS && dimItf != nullptr) {
    res = (*dimItf)->RegisterCallback(dimItf, DynamicInterfaceCallback,
                                      &dimCalls);
    if (res != XA_RESULT_SUCCESS) {
      return Fail(env, "dim RegisterCallback", res);
    }
    dimNote = "dim callback registered";
  } else if (res == XA_RESULT_FEATURE_UNSUPPORTED) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "SKIP: media player does not expose "
                        "XA_IID_DYNAMICINTERFACEMANAGEMENT (no init hook)");
    dimNote = "dim skip (not exposed on this platform)";
  } else {
    return Fail(env, "GetInterface(XA_IID_DYNAMICINTERFACEMANAGEMENT)", res);
  }

  // XASeekItf. Loop disabled across the whole stream is both the interface's
  // own initial state and the only argument triple the Android backend accepts:
  // a non-zero start or a bounded end is rejected feature-unsupported, and a
  // start not below the end is rejected parameter-invalid. So the values must
  // come back exactly as set.
  XASeekItf seekItf = nullptr;
  res = (*playerObj)->GetInterface(playerObj, XA_IID_SEEK, &seekItf);
  if (res != XA_RESULT_SUCCESS || seekItf == nullptr) {
    return Fail(env, "GetInterface(XA_IID_SEEK)", res);
  }
  res = (*seekItf)->SetLoop(seekItf, XA_BOOLEAN_FALSE, 0, XA_TIME_UNKNOWN);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "Seek SetLoop", res);
  XAboolean loopEnabled = XA_BOOLEAN_TRUE;
  XAmillisecond loopStart = 0xdeadbeef;
  XAmillisecond loopEnd = 0xdeadbeef;
  res = (*seekItf)->GetLoop(seekItf, &loopEnabled, &loopStart, &loopEnd);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "Seek GetLoop", res);
  if (loopEnabled != XA_BOOLEAN_FALSE || loopStart != 0 ||
      loopEnd != XA_TIME_UNKNOWN) {
    return Fail(env, "Seek GetLoop round-trip", loopEnabled, loopStart);
  }
  // A position seek is accepted in either defined mode whatever the data source
  // is; an undefined mode must be rejected rather than passed through.
  res = (*seekItf)->SetPosition(seekItf, 0, XA_SEEKMODE_FAST);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "Seek SetPosition fast", res);
  res = (*seekItf)->SetPosition(seekItf, 0, 0);
  if (res != XA_RESULT_PARAMETER_INVALID) {
    return Fail(env, "Seek SetPosition undefined mode expected rejection", res);
  }

  // XAPrefetchStatusItf. No data is ever streamed, so no callback is expected
  // to fire; the registration and the getter round-trips are the checks.
  XAPrefetchStatusItf prefetchItf = nullptr;
  res = (*playerObj)->GetInterface(playerObj, XA_IID_PREFETCHSTATUS,
                                   &prefetchItf);
  if (res != XA_RESULT_SUCCESS || prefetchItf == nullptr) {
    return Fail(env, "GetInterface(XA_IID_PREFETCHSTATUS)", res);
  }
  res = (*prefetchItf)->RegisterCallback(prefetchItf, PrefetchCallback,
                                         &prefetchCalls);
  if (res != XA_RESULT_SUCCESS) {
    return Fail(env, "prefetch RegisterCallback", res);
  }
  const XAuint32 kPrefetchEvents =
      XA_PREFETCHEVENT_STATUSCHANGE | XA_PREFETCHEVENT_FILLLEVELCHANGE;
  res = (*prefetchItf)->SetCallbackEventsMask(prefetchItf, kPrefetchEvents);
  if (res != XA_RESULT_SUCCESS) {
    return Fail(env, "prefetch SetCallbackEventsMask", res);
  }
  XAuint32 prefetchEvents = 0;
  res = (*prefetchItf)->GetCallbackEventsMask(prefetchItf, &prefetchEvents);
  if (res != XA_RESULT_SUCCESS || prefetchEvents != kPrefetchEvents) {
    return Fail(env, "prefetch events mask round-trip", res, prefetchEvents);
  }
  XAuint32 prefetchStatus = 0;
  res = (*prefetchItf)->GetPrefetchStatus(prefetchItf, &prefetchStatus);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "GetPrefetchStatus", res);
  if (prefetchStatus != XA_PREFETCHSTATUS_UNDERFLOW &&
      prefetchStatus != XA_PREFETCHSTATUS_SUFFICIENTDATA &&
      prefetchStatus != XA_PREFETCHSTATUS_OVERFLOW) {
    return Fail(env, "GetPrefetchStatus value", res, prefetchStatus);
  }
  XApermille fillLevel = -1;
  res = (*prefetchItf)->GetFillLevel(prefetchItf, &fillLevel);
  if (res != XA_RESULT_SUCCESS || fillLevel < 0 || fillLevel > 1000) {
    return Fail(env, "GetFillLevel", res, static_cast<XAuint32>(fillLevel));
  }
  // A zero fill-update period is meaningless and must be rejected; a legal one
  // must read back exactly.
  res = (*prefetchItf)->SetFillUpdatePeriod(prefetchItf, 0);
  if (res != XA_RESULT_PARAMETER_INVALID) {
    return Fail(env, "SetFillUpdatePeriod(0) expected rejection", res);
  }
  res = (*prefetchItf)->SetFillUpdatePeriod(prefetchItf, 250);
  if (res != XA_RESULT_SUCCESS) return Fail(env, "SetFillUpdatePeriod", res);
  XApermille fillPeriod = 0;
  res = (*prefetchItf)->GetFillUpdatePeriod(prefetchItf, &fillPeriod);
  if (res != XA_RESULT_SUCCESS || fillPeriod != 250) {
    return Fail(env, "fill update period round-trip", res,
                static_cast<XAuint32>(fillPeriod));
  }

  // The player holds a strong reference to its sink mix, so it goes first.
  (*playerObj)->Destroy(playerObj);
  (*playerMixObj)->Destroy(playerMixObj);
  std::string playerNote =
      "mediaplayer seek+prefetch ok, " + dimNote + " (prefetch-status=" +
      std::to_string(prefetchStatus) + " fill-level=" +
      std::to_string(static_cast<int>(fillLevel)) + " dim-callbacks=" +
      std::to_string(dimCalls.count.load(std::memory_order_relaxed)) +
      " prefetch-callbacks=" +
      std::to_string(prefetchCalls.count.load(std::memory_order_relaxed)) + ")";

  // --- 9. Engine destroyed last; nothing called after -----------------------
  (*engineObj)->Destroy(engineObj);

  std::string msg = "helloopenmaxal OK: engine realized; itf-counts" +
                    classCounts + "; global-engine-itfs=" +
                    std::to_string(nGlobal) + "; " + volNote + "; " + playNote +
                    "; " + asyncNote + "; " + playerNote;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
