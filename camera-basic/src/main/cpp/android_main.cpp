/*
 * Copyright (C) 2017 The Android Open Source Project
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

#include "utils/native_debug.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "camera_engine.h"

/*
 * SampleEngine global object
 *
 * Published from android_main on the NativeActivity thread, but read from the
 * Java side's JNI entry points, which run on other threads. The camera
 * permission callback in particular can arrive before android_main has run --
 * when the permission is already granted, the Java side answers immediately --
 * so readers must wait for publication rather than assume it.
 *
 * The window is small on native hardware and much larger under binary
 * translation, where thread startup is slower; the race is the same either way.
 */
static std::mutex engineMutex;
static std::condition_variable engineReady;
static CameraEngine* pEngineObj = nullptr;

static void SetAppEngine(CameraEngine* engine) {
  {
    std::lock_guard<std::mutex> lock(engineMutex);
    pEngineObj = engine;
  }
  engineReady.notify_all();
}

CameraEngine* GetAppEngine(void) {
  std::lock_guard<std::mutex> lock(engineMutex);
  ASSERT(pEngineObj, "AppEngine has not initialized");
  return pEngineObj;
}

/*
 * Block until android_main publishes the engine, for callers that may run
 * before it does. Returns nullptr if it does not appear in time, which is a
 * real failure rather than a race and is left for the caller to report.
 * Must not be called on a thread that cannot afford to block.
 */
CameraEngine* WaitForAppEngine(void) {
  std::unique_lock<std::mutex> lock(engineMutex);
  engineReady.wait_for(lock, std::chrono::seconds(10),
                       [] { return pEngineObj != nullptr; });
  return pEngineObj;
}

/**
 * Teamplate function for NativeActivity derived applications
 *   Create/Delete camera object with
 *   INIT_WINDOW/TERM_WINDOW command, ignoring other event.
 */
static void ProcessAndroidCmd(struct android_app* app, int32_t cmd) {
  CameraEngine* engine = reinterpret_cast<CameraEngine*>(app->userData);
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (engine->AndroidApp()->window != NULL) {
        engine->SaveNativeWinRes(ANativeWindow_getWidth(app->window),
                                 ANativeWindow_getHeight(app->window),
                                 ANativeWindow_getFormat(app->window));
        engine->OnAppInitWindow();
      }
      break;
    case APP_CMD_TERM_WINDOW:
      engine->OnAppTermWindow();
      ANativeWindow_setBuffersGeometry(
          app->window, engine->GetSavedNativeWinWidth(),
          engine->GetSavedNativeWinHeight(), engine->GetSavedNativeWinFormat());
      break;
    case APP_CMD_CONFIG_CHANGED:
      engine->OnAppConfigChange();
      break;
    case APP_CMD_LOST_FOCUS:
      break;
  }
}

extern "C" void android_main(struct android_app* state) {
  CameraEngine engine(state);
  SetAppEngine(&engine);

  state->userData = reinterpret_cast<void*>(&engine);
  state->onAppCmd = ProcessAndroidCmd;

  // loop waiting for stuff to do.
  while (!state->destroyRequested) {
    struct android_poll_source* source = nullptr;
    auto result = ALooper_pollOnce(0, NULL, nullptr, (void**)&source);
    ASSERT(result != ALOOPER_POLL_ERROR, "ALooper_pollOnce returned an error");
    if (source != NULL) {
      source->process(state, source);
    }
    pEngineObj->DrawFrame();
  }

  LOGI("CameraEngine thread destroy requested!");
  engine.DeleteCamera();
  SetAppEngine(nullptr);
}

/**
 * Handle Android System APP_CMD_INIT_WINDOW message
 *   Request camera persmission from Java side
 *   Create camera object if camera has been granted
 */
void CameraEngine::OnAppInitWindow(void) {
  if (!cameraGranted_) {
    // Not permitted to use camera yet, ask(again) and defer other events
    RequestCameraPermission();
    return;
  }

  rotation_ = GetDisplayRotation();

  CreateCamera();
  ASSERT(camera_, "CameraCreation Failed");

  EnableUI();

  // NativeActivity end is ready to display, start pulling images
  cameraReady_ = true;
  camera_->StartPreview(true);
}

/**
 * Handle APP_CMD_TEMR_WINDOW
 */
void CameraEngine::OnAppTermWindow(void) {
  cameraReady_ = false;
  DeleteCamera();
}

/**
 * Handle APP_CMD_CONFIG_CHANGED
 */
void CameraEngine::OnAppConfigChange(void) {
  int newRotation = GetDisplayRotation();

  if (newRotation != rotation_) {
    OnAppTermWindow();

    rotation_ = newRotation;
    OnAppInitWindow();
  }
}

/**
 * Retrieve saved native window width.
 * @return width of native window
 */
int32_t CameraEngine::GetSavedNativeWinWidth(void) {
  return savedNativeWinRes_.width;
}

/**
 * Retrieve saved native window height.
 * @return height of native window
 */
int32_t CameraEngine::GetSavedNativeWinHeight(void) {
  return savedNativeWinRes_.height;
}

/**
 * Retrieve saved native window format
 * @return format of native window
 */
int32_t CameraEngine::GetSavedNativeWinFormat(void) {
  return savedNativeWinRes_.format;
}

/**
 * Save original NativeWindow Resolution
 * @param w width of native window in pixel
 * @param h height of native window in pixel
 * @param format
 */
void CameraEngine::SaveNativeWinRes(int32_t w, int32_t h, int32_t format) {
  savedNativeWinRes_.width = w;
  savedNativeWinRes_.height = h;
  savedNativeWinRes_.format = format;
}
