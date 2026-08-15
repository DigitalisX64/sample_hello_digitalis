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

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#define LOG_TAG "helloopensles"

namespace {

// Every (*itf)->Method(itf, ...) below is a through-the-vtable call. The
// Register*Callback entry points are the interesting ones: they hand a GUEST
// function pointer to the host implementation, so they only work if the proxy
// wraps it into a host-callable thunk. Establishing that those registrations
// return cleanly - not producing audio - is what this probe is for.

std::atomic<int> g_dim_callbacks{0};
std::atomic<int> g_mix_device_change_callbacks{0};
std::atomic<int> g_abq_callbacks{0};
std::atomic<int> g_pcm_callbacks{0};
std::atomic<int> g_audio_io_callbacks{0};

// Callbacks touch nothing but atomics. Re-entering OpenSL ES from inside a
// callback is forbidden by the specification and deadlocks on the object mutex.

void SLAPIENTRY DimCallback(SLDynamicInterfaceManagementItf /*caller*/,
                            void* /*pContext*/, SLuint32 event, SLresult result,
                            const SLInterfaceID /*iid*/) {
  g_dim_callbacks.fetch_add(1);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "dynamic-interface callback: event=%u result=%u",
                      static_cast<unsigned>(event),
                      static_cast<unsigned>(result));
}

void SLAPIENTRY MixDeviceChangeCallback(SLOutputMixItf /*caller*/,
                                        void* /*pContext*/) {
  g_mix_device_change_callbacks.fetch_add(1);
}

void SLAPIENTRY AudioInputsChangedCallback(SLAudioIODeviceCapabilitiesItf
                                               /*caller*/,
                                           void* /*pContext*/,
                                           SLuint32 /*deviceID*/,
                                           SLint32 /*numInputs*/,
                                           SLboolean /*isNew*/) {
  g_audio_io_callbacks.fetch_add(1);
}

void SLAPIENTRY AudioOutputsChangedCallback(SLAudioIODeviceCapabilitiesItf
                                                /*caller*/,
                                            void* /*pContext*/,
                                            SLuint32 /*deviceID*/,
                                            SLint32 /*numOutputs*/,
                                            SLboolean /*isNew*/) {
  g_audio_io_callbacks.fetch_add(1);
}

void SLAPIENTRY DefaultDeviceIDMapChangedCallback(SLAudioIODeviceCapabilitiesItf
                                                      /*caller*/,
                                                  void* /*pContext*/,
                                                  SLboolean /*isOutput*/,
                                                  SLint32 /*numDevices*/) {
  g_audio_io_callbacks.fetch_add(1);
}

// Signature is slAndroidBufferQueueCallback: it returns SLresult, unlike every
// other callback in the API.
SLresult SLAPIENTRY AbqCallback(SLAndroidBufferQueueItf /*caller*/,
                                void* /*pCallbackContext*/,
                                void* /*pBufferContext*/,
                                void* /*pBufferData*/, SLuint32 /*dataSize*/,
                                SLuint32 /*dataUsed*/,
                                const SLAndroidBufferItem* /*pItems*/,
                                SLuint32 /*itemsLength*/) {
  g_abq_callbacks.fetch_add(1);
  return SL_RESULT_SUCCESS;
}

void SLAPIENTRY PcmQueueCallback(SLAndroidSimpleBufferQueueItf /*caller*/,
                                 void* /*pContext*/) {
  g_pcm_callbacks.fetch_add(1);
}

const char* kProbeContext = "helloopensles-context";

jstring Fail(JNIEnv* env, const char* tag, SLuint32 result, SLuint32 want = 0) {
  char m[192];
  snprintf(m, sizeof(m),
           "helloopensles FAIL at %s: result=0x%08x expected=0x%08x", tag,
           static_cast<unsigned>(result), static_cast<unsigned>(want));
  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s", m);
  return env->NewStringUTF(m);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloopensles_MainActivity_probeOpenSLES(JNIEnv* env,
                                                          jobject /*this*/) {
#define REQUIRE(tag, call, want)                              \
  do {                                                        \
    const SLresult r__ = (call);                              \
    if (r__ != static_cast<SLresult>(want)) {                 \
      return Fail(env, tag, r__, static_cast<SLuint32>(want)); \
    }                                                         \
  } while (0)

  // --- 1. Engine ------------------------------------------------------------
  SLObjectItf engineObj = nullptr;
  REQUIRE("slCreateEngine",
          slCreateEngine(&engineObj, 0, nullptr, 0, nullptr, nullptr),
          SL_RESULT_SUCCESS);
  if (engineObj == nullptr) return Fail(env, "engine object null", 0);
  REQUIRE("engine Realize", (*engineObj)->Realize(engineObj, SL_BOOLEAN_FALSE),
          SL_RESULT_SUCCESS);
  SLuint32 state = 0;
  REQUIRE("engine GetState", (*engineObj)->GetState(engineObj, &state),
          SL_RESULT_SUCCESS);
  if (state != SL_OBJECT_STATE_REALIZED) {
    return Fail(env, "engine state", state, SL_OBJECT_STATE_REALIZED);
  }
  SLEngineItf engineItf = nullptr;
  REQUIRE("GetInterface(SL_IID_ENGINE)",
          (*engineObj)->GetInterface(engineObj, SL_IID_ENGINE, &engineItf),
          SL_RESULT_SUCCESS);
  if (engineItf == nullptr) return Fail(env, "engine itf null", 0);

  // --- 2. DynamicInterfaceManagement callback -------------------------------
  // MPH_DYNAMICINTERFACEMANAGEMENT is INTERFACE_IMPLICIT on every class, so it
  // needs no request at creation and is available once realized.
  SLDynamicInterfaceManagementItf dimItf = nullptr;
  REQUIRE("GetInterface(SL_IID_DYNAMICINTERFACEMANAGEMENT)",
          (*engineObj)->GetInterface(engineObj,
                                     SL_IID_DYNAMICINTERFACEMANAGEMENT,
                                     &dimItf),
          SL_RESULT_SUCCESS);
  if (dimItf == nullptr) return Fail(env, "dynamic-interface itf null", 0);
  REQUIRE("SLDynamicInterfaceManagementItf::RegisterCallback",
          (*dimItf)->RegisterCallback(dimItf, DimCallback,
                                      const_cast<char*>(kProbeContext)),
          SL_RESULT_SUCCESS);
  // Liveness of a second slot on the same vtable, with arguments that reach the
  // implementation and come back as a specific code: SL_IID_MIDIMESSAGE is not
  // in the engine class at all, so AddInterface must reject it outright. No
  // state machine is entered and nothing is added, so this has no side effect.
  REQUIRE("SLDynamicInterfaceManagementItf::AddInterface(midi)",
          (*dimItf)->AddInterface(dimItf, SL_IID_MIDIMESSAGE, SL_BOOLEAN_FALSE),
          SL_RESULT_FEATURE_UNSUPPORTED);

  // --- 3. AudioIODeviceCapabilities callbacks -------------------------------
  // Android builds wilhelm with -DUSE_PROFILES=0, which turns this interface's
  // INTERFACE_IMPLICIT_BASE relation into INTERFACE_UNAVAILABLE, so the engine
  // never exposes it and GetInterface reports feature-unsupported. The three
  // registration entry points therefore cannot be reached by an application on
  // this platform; the assertion is that the query is rejected cleanly. Should
  // a platform expose it, all three are exercised and must succeed.
  std::string audioIoNote;
  SLAudioIODeviceCapabilitiesItf audioIoItf = nullptr;
  SLresult res = (*engineObj)->GetInterface(
      engineObj, SL_IID_AUDIOIODEVICECAPABILITIES, &audioIoItf);
  if (res == SL_RESULT_SUCCESS && audioIoItf != nullptr) {
    REQUIRE("RegisterAvailableAudioInputsChangedCallback",
            (*audioIoItf)->RegisterAvailableAudioInputsChangedCallback(
                audioIoItf, AudioInputsChangedCallback,
                const_cast<char*>(kProbeContext)),
            SL_RESULT_SUCCESS);
    REQUIRE("RegisterAvailableAudioOutputsChangedCallback",
            (*audioIoItf)->RegisterAvailableAudioOutputsChangedCallback(
                audioIoItf, AudioOutputsChangedCallback,
                const_cast<char*>(kProbeContext)),
            SL_RESULT_SUCCESS);
    REQUIRE("RegisterDefaultDeviceIDMapChangedCallback",
            (*audioIoItf)->RegisterDefaultDeviceIDMapChangedCallback(
                audioIoItf, DefaultDeviceIDMapChangedCallback,
                const_cast<char*>(kProbeContext)),
            SL_RESULT_SUCCESS);
    audioIoNote = "audio-io-device-caps all three registered";
  } else if (res == SL_RESULT_FEATURE_UNSUPPORTED) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "SKIP: engine does not expose "
                        "SL_IID_AUDIOIODEVICECAPABILITIES (profiles disabled)");
    audioIoNote = "audio-io-device-caps skip (unavailable in this profile)";
  } else {
    return Fail(env, "GetInterface(SL_IID_AUDIOIODEVICECAPABILITIES)", res,
                SL_RESULT_FEATURE_UNSUPPORTED);
  }

  // --- 4. OutputMix device-change callback ----------------------------------
  SLObjectItf mixObj = nullptr;
  REQUIRE("CreateOutputMix",
          (*engineItf)->CreateOutputMix(engineItf, &mixObj, 0, nullptr, nullptr),
          SL_RESULT_SUCCESS);
  if (mixObj == nullptr) return Fail(env, "outputmix object null", 0);
  REQUIRE("outputmix Realize", (*mixObj)->Realize(mixObj, SL_BOOLEAN_FALSE),
          SL_RESULT_SUCCESS);
  SLOutputMixItf mixItf = nullptr;
  REQUIRE("GetInterface(SL_IID_OUTPUTMIX)",
          (*mixObj)->GetInterface(mixObj, SL_IID_OUTPUTMIX, &mixItf),
          SL_RESULT_SUCCESS);
  if (mixItf == nullptr) return Fail(env, "outputmix itf null", 0);
  REQUIRE("SLOutputMixItf::RegisterDeviceChangeCallback",
          (*mixItf)->RegisterDeviceChangeCallback(
              mixItf, MixDeviceChangeCallback,
              const_cast<char*>(kProbeContext)),
          SL_RESULT_SUCCESS);
  // Out-parameter marshalling on the same vtable: querying with a null array
  // must report how many destination devices exist.
  SLint32 numDevices = 0;
  REQUIRE("GetDestinationOutputDeviceIDs",
          (*mixItf)->GetDestinationOutputDeviceIDs(mixItf, &numDevices, nullptr),
          SL_RESULT_SUCCESS);
  if (numDevices < 1) {
    return Fail(env, "destination device count",
                static_cast<SLuint32>(numDevices), 1);
  }

  // --- 5. AndroidBufferQueue callback (AAC ADTS streaming decode) -----------
  // Source is an Android buffer queue fed raw ADTS; sink is a PCM buffer queue.
  // That pairing, not an output mix, is what selects the ADTS decoder inside
  // the implementation - an output-mix sink with an ADTS MIME source is
  // rejected as an invalid parameter.
  SLDataLocator_AndroidBufferQueue srcAbq = {SL_DATALOCATOR_ANDROIDBUFFERQUEUE,
                                             2 /*numBuffers*/};
  SLDataFormat_MIME srcMime = {SL_DATAFORMAT_MIME, SL_ANDROID_MIME_AACADTS,
                               SL_CONTAINERTYPE_RAW};
  SLDataSource decSource = {&srcAbq, &srcMime};

  SLDataLocator_AndroidSimpleBufferQueue dstBq = {
      SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2 /*numBuffers*/};
  // The PCM fields must be well formed but are ignored: the decoded format is
  // taken from the stream.
  SLDataFormat_PCM dstPcm = {SL_DATAFORMAT_PCM,
                             1 /*numChannels*/,
                             SL_SAMPLINGRATE_8,
                             SL_PCMSAMPLEFORMAT_FIXED_16,
                             16 /*containerSize*/,
                             SL_SPEAKER_FRONT_LEFT,
                             SL_BYTEORDER_LITTLEENDIAN};
  SLDataSink decSink = {&dstBq, &dstPcm};

  // Both buffer-queue interfaces are explicit on the audio player class, so
  // they have to be requested here or GetInterface will refuse them later.
  const SLInterfaceID playerIids[2] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                       SL_IID_ANDROIDBUFFERQUEUESOURCE};
  const SLboolean playerReq[2] = {SL_BOOLEAN_TRUE, SL_BOOLEAN_TRUE};

  SLObjectItf playerObj = nullptr;
  REQUIRE("CreateAudioPlayer(adts-abq)",
          (*engineItf)->CreateAudioPlayer(engineItf, &playerObj, &decSource,
                                          &decSink, 2, playerIids, playerReq),
          SL_RESULT_SUCCESS);
  if (playerObj == nullptr) return Fail(env, "player object null", 0);
  REQUIRE("player Realize", (*playerObj)->Realize(playerObj, SL_BOOLEAN_FALSE),
          SL_RESULT_SUCCESS);

  SLAndroidBufferQueueItf abqItf = nullptr;
  REQUIRE("GetInterface(SL_IID_ANDROIDBUFFERQUEUESOURCE)",
          (*playerObj)->GetInterface(playerObj, SL_IID_ANDROIDBUFFERQUEUESOURCE,
                                     &abqItf),
          SL_RESULT_SUCCESS);
  if (abqItf == nullptr) return Fail(env, "android-buffer-queue itf null", 0);
  // Registration is only legal while the player is stopped, which is its state
  // straight after a synchronous realize.
  REQUIRE("SLAndroidBufferQueueItf::RegisterCallback",
          (*abqItf)->RegisterCallback(abqItf, AbqCallback,
                                      const_cast<char*>(kProbeContext)),
          SL_RESULT_SUCCESS);
  REQUIRE("SLAndroidBufferQueueItf::SetCallbackEventsMask",
          (*abqItf)->SetCallbackEventsMask(
              abqItf, SL_ANDROIDBUFFERQUEUEEVENT_PROCESSED),
          SL_RESULT_SUCCESS);
  SLuint32 eventMask = 0;
  REQUIRE("SLAndroidBufferQueueItf::GetCallbackEventsMask",
          (*abqItf)->GetCallbackEventsMask(abqItf, &eventMask),
          SL_RESULT_SUCCESS);
  if (eventMask != SL_ANDROIDBUFFERQUEUEEVENT_PROCESSED) {
    return Fail(env, "callback events mask read-back", eventMask,
                SL_ANDROIDBUFFERQUEUEEVENT_PROCESSED);
  }

  // The PCM sink queue's own registration goes through a different proxy slot;
  // exercising it here guards that already-working path against regression.
  SLAndroidSimpleBufferQueueItf pcmItf = nullptr;
  REQUIRE("GetInterface(SL_IID_ANDROIDSIMPLEBUFFERQUEUE)",
          (*playerObj)->GetInterface(playerObj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE,
                                     &pcmItf),
          SL_RESULT_SUCCESS);
  if (pcmItf == nullptr) return Fail(env, "simple-buffer-queue itf null", 0);
  REQUIRE("SLAndroidSimpleBufferQueueItf::RegisterCallback",
          (*pcmItf)->RegisterCallback(pcmItf, PcmQueueCallback,
                                      const_cast<char*>(kProbeContext)),
          SL_RESULT_SUCCESS);

  // Best-effort delivery check. A pure-command item carries no data: the whole
  // message is one item key plus its size field, hence an items length of two
  // 32-bit words and a null data pointer with zero length.
  SLAndroidBufferItem eosItem;
  eosItem.itemKey = SL_ANDROID_ITEMKEY_EOS;
  eosItem.itemSize = 0;
  REQUIRE("SLAndroidBufferQueueItf::Enqueue(eos)",
          (*abqItf)->Enqueue(abqItf, const_cast<char*>(kProbeContext),
                             nullptr /*pData*/, 0 /*dataLength*/, &eosItem,
                             sizeof(SLuint32) * 2),
          SL_RESULT_SUCCESS);

  SLPlayItf playItf = nullptr;
  REQUIRE("GetInterface(SL_IID_PLAY)",
          (*playerObj)->GetInterface(playerObj, SL_IID_PLAY, &playItf),
          SL_RESULT_SUCCESS);
  if (playItf == nullptr) return Fail(env, "play itf null", 0);
  REQUIRE("SetPlayState(playing)",
          (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_PLAYING),
          SL_RESULT_SUCCESS);

  std::string abqNote;
  bool abqFired = false;
  for (int i = 0; i < 200 && !abqFired; ++i) {
    abqFired = g_abq_callbacks.load() > 0;
    if (!abqFired) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (abqFired) {
    abqNote = "android-buffer-queue callback fired";
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", abqNote.c_str());
  } else {
    // The decoder reads the queue as soon as the player is realized, so it can
    // reach end-of-stream before the command item is enqueued and then never
    // consume it. Delivery is opportunistic here; the registration results
    // above are the assertions.
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "SKIP: android-buffer-queue callback not delivered "
                        "within the wait window");
    abqNote = "android-buffer-queue callback skip (not delivered)";
  }
  REQUIRE("SetPlayState(stopped)",
          (*playItf)->SetPlayState(playItf, SL_PLAYSTATE_STOPPED),
          SL_RESULT_SUCCESS);

  // --- 6. Interfaces this profile cannot hand out ---------------------------
  // These must be refused with a specific code, never abort and never succeed.
  // SL_IID_MIDIMESSAGE is absent from both class tables; SL_IID_VISUALIZATION
  // is present on the audio player but relates as optional, which the disabled
  // optional profile turns into unavailable.
  void* unobtainable = nullptr;
  REQUIRE("GetInterface(SL_IID_MIDIMESSAGE) on engine",
          (*engineObj)->GetInterface(engineObj, SL_IID_MIDIMESSAGE,
                                     &unobtainable),
          SL_RESULT_FEATURE_UNSUPPORTED);
  REQUIRE("GetInterface(SL_IID_MIDIMESSAGE) on player",
          (*playerObj)->GetInterface(playerObj, SL_IID_MIDIMESSAGE,
                                     &unobtainable),
          SL_RESULT_FEATURE_UNSUPPORTED);
  REQUIRE("GetInterface(SL_IID_VISUALIZATION) on player",
          (*playerObj)->GetInterface(playerObj, SL_IID_VISUALIZATION,
                                     &unobtainable),
          SL_RESULT_FEATURE_UNSUPPORTED);

  // --- 7. Teardown, innermost object first ----------------------------------
  (*playerObj)->Destroy(playerObj);
  (*mixObj)->Destroy(mixObj);
  (*engineObj)->Destroy(engineObj);

  std::string msg =
      "helloopensles OK: engine realized; dynamic-interface callback "
      "registered; output-mix device-change callback registered (" +
      std::to_string(static_cast<int>(numDevices)) +
      " destination device(s)); android-buffer-queue callback registered and "
      "event mask verified; simple-buffer-queue callback registered; "
      "unobtainable interfaces refused cleanly; " +
      audioIoNote + "; " + abqNote;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());

#undef REQUIRE
}
