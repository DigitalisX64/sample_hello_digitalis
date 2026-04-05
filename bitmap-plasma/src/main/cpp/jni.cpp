// Copyright (C) 2025 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0

#include <jni.h>

#include "plasma.h"

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void*) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  jclass c = env->FindClass("com/example/plasma/PlasmaView");
  if (c == nullptr) return JNI_ERR;

  static const JNINativeMethod methods[] = {
      {"renderPlasma", "(Landroid/graphics/Bitmap;J)V",
       reinterpret_cast<void*>(RenderPlasma)},
  };
  int rc = env->RegisterNatives(c, methods,
                                sizeof(methods) / sizeof(methods[0]));
  if (rc != JNI_OK) return rc;

  return JNI_VERSION_1_6;
}
