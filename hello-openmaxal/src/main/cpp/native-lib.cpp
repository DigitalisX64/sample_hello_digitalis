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

  // --- 8. Engine destroyed last; nothing called after -----------------------
  (*engineObj)->Destroy(engineObj);

  std::string msg = "helloopenmaxal OK: engine realized; itf-counts" +
                    classCounts + "; global-engine-itfs=" +
                    std::to_string(nGlobal) + "; " + volNote + "; " + playNote +
                    "; " + asyncNote;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
