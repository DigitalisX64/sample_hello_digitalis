/*
 * Copyright (C) The Android Open Source Project
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

#include <android/log.h>
#include <errno.h>
#include <jni.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <string.h>

const char kLogTag[] = "orderfiledemo";

#ifdef GENERATE_PROFILES
extern "C" int __llvm_profile_set_filename(const char*);
extern "C" int __llvm_profile_initialize_file(void);
extern "C" int __llvm_profile_dump(void);
#endif

void DumpProfileDataIfNeeded(const char* temp_dir) {
#ifdef GENERATE_PROFILES
  char profile_location[PATH_MAX] = {};
  snprintf(profile_location, sizeof(profile_location), "%s/demo.profraw",
           temp_dir);
  if (__llvm_profile_set_filename(profile_location) == -1) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "__llvm_profile_set_filename(\"%s\") failed: %s",
                        profile_location, strerror(errno));
    return;
  }

  if (__llvm_profile_initialize_file() == -1) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "__llvm_profile_initialize_file failed: %s",
                        strerror(errno));
    return;
  }

  if (__llvm_profile_dump() == -1) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "__llvm_profile_dump() failed: %s", strerror(errno));
    return;
  }
  __android_log_print(ANDROID_LOG_DEBUG, kLogTag, "Wrote profile data to %s",
                      profile_location);
#else
  (void)temp_dir; // To avoid unused-parameter warning
  __android_log_print(ANDROID_LOG_DEBUG, kLogTag,
                      "Did not write profile data because the app was not "
                      "built for profile generation");
#endif
}

void RunWorkload(JNIEnv* env, jobject /* this */, jstring temp_dir) {
  DumpProfileDataIfNeeded(env->GetStringUTFChars(temp_dir, 0));
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* _Nonnull vm,
                                             void* _Nullable) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  jclass c = env->FindClass("com/example/hellodigitalis/orderfile/MainActivity");
  if (c == nullptr) return JNI_ERR;

  static const JNINativeMethod methods[] = {
      {"runWorkload", "(Ljava/lang/String;)V",
       reinterpret_cast<void*>(RunWorkload)},
  };
  int rc = env->RegisterNatives(c, methods, sizeof(methods) / sizeof(methods[0]));
  if (rc != JNI_OK) return rc;

  return JNI_VERSION_1_6;
}
