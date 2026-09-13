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

// Narrow integer results across the proxy boundary.
//
// A function that returns bool, jboolean, jbyte, jshort or jchar hands its result
// back in x0. Real callees write it with a w-register store (`mov w0, #v`), which
// leaves w0 holding the value zero-extended (bool, unsigned) or sign-extended
// (signed) to 32 bits. Callers rely on that: rustc tests a jboolean with
// `cbz w0`. A proxy that writes only the low sizeof(T) bytes of x0 leaves the rest
// of the register holding whatever the caller passed in x0 -- the JNIEnv*, the
// AMediaFormat* -- so a host `false` reads back as true. Signal's ringrtc (Rust,
// jni-rs 0.22) took exactly that path: ExceptionCheck() "returned" true with no
// exception pending, jni-rs panicked, and ringrtcGetBuildInfo() returned null.
//
// C and C++ never see this, because clang narrows the result itself
// (`tst w0, #0xff`). So every probe here goes through CallRaw (call_raw.S), which
// returns the callee's x0 untouched, and checks w0 the way a Rust caller would.
// The first argument is always a live pointer, so stale register contents are
// never accidentally zero.

#include <jni.h>
#include <android/log.h>
#include <media/NdkMediaFormat.h>

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "HelloNarrowRet"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" uint64_t CallRaw(void* fn, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3);

namespace {

uint64_t U(const void* p) { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename Fn>
void* F(Fn fn) { return reinterpret_cast<void*>(fn); }

struct Report {
  std::string text;
  int checks = 0;
  int bad = 0;
};

void Expect(Report& r, const char* what, uint64_t x0, uint32_t want_w0) {
  uint32_t w0 = static_cast<uint32_t>(x0);
  char line[256];
  r.checks++;
  if (w0 == want_w0) {
    snprintf(line, sizeof(line), "PASS %s: w0=0x%08" PRIx32, what, w0);
    LOGI("%s", line);
  } else {
    r.bad++;
    snprintf(line, sizeof(line), "FAIL %s: w0=0x%08" PRIx32 " want 0x%08" PRIx32 " (x0=0x%016" PRIx64 ")",
             what, w0, want_w0, x0);
    LOGE("%s", line);
  }
  r.text += line;
  r.text += '\n';
}

void Missing(Report& r, const char* what) {
  char line[256];
  r.checks++;
  r.bad++;
  snprintf(line, sizeof(line), "FAIL %s: lookup failed", what);
  LOGE("%s", line);
  r.text += line;
  r.text += '\n';
}

// jboolean ExceptionCheck(JNIEnv*): the exact call shape that broke Signal.
void ProbeExceptionCheck(Report& r, JNIEnv* env) {
  const JNINativeInterface* fns = env->functions;
  Expect(r, "JNIEnv::ExceptionCheck, nothing pending",
         CallRaw(F(fns->ExceptionCheck), U(env), 0, 0, 0), 0);

  jclass runtime_exception = env->FindClass("java/lang/RuntimeException");
  if (runtime_exception == nullptr) {
    env->ExceptionClear();
    Missing(r, "java/lang/RuntimeException");
    return;
  }
  env->ThrowNew(runtime_exception, "hello-narrowret: deliberately pending");
  uint64_t x0 = CallRaw(F(fns->ExceptionCheck), U(env), 0, 0, 0);
  env->ExceptionClear();
  env->DeleteLocalRef(runtime_exception);
  Expect(r, "JNIEnv::ExceptionCheck, exception pending", x0, 1);
}

// jboolean IsSameObject(JNIEnv*, jobject, jobject).
void ProbeIsSameObject(Report& r, JNIEnv* env, jobject thiz) {
  const JNINativeInterface* fns = env->functions;
  Expect(r, "JNIEnv::IsSameObject, different",
         CallRaw(F(fns->IsSameObject), U(env), U(thiz), 0, 0), 0);
  Expect(r, "JNIEnv::IsSameObject, same",
         CallRaw(F(fns->IsSameObject), U(env), U(thiz), U(thiz), 0), 1);
}

// Call<Type>MethodA for every narrow Java primitive, via static methods on the activity.
void ProbeStaticCalls(Report& r, JNIEnv* env, jobject thiz) {
  const JNINativeInterface* fns = env->functions;
  jclass cls = env->GetObjectClass(thiz);

  struct Case {
    const char* what;
    const char* name;
    const char* sig;
    void* call;
    uint32_t want_w0;
  };
  const Case cases[] = {
      {"CallStaticBooleanMethodA -> false", "alwaysFalse", "()Z", F(fns->CallStaticBooleanMethodA), 0},
      {"CallStaticBooleanMethodA -> true", "alwaysTrue", "()Z", F(fns->CallStaticBooleanMethodA), 1},
      // Signed results are sign-extended to 32 bits, as `mov w0, #-1` leaves them.
      {"CallStaticByteMethodA -> -1", "minusOneByte", "()B", F(fns->CallStaticByteMethodA), 0xffffffff},
      {"CallStaticShortMethodA -> -1", "minusOneShort", "()S", F(fns->CallStaticShortMethodA), 0xffffffff},
      {"CallStaticCharMethodA -> 0xffff", "maxChar", "()C", F(fns->CallStaticCharMethodA), 0x0000ffff},
  };
  for (const Case& c : cases) {
    jmethodID mid = env->GetStaticMethodID(cls, c.name, c.sig);
    if (mid == nullptr) {
      env->ExceptionClear();
      Missing(r, c.what);
      continue;
    }
    jvalue no_args[1] = {};
    uint64_t x0 = CallRaw(c.call, U(env), U(cls), U(mid), U(no_args));
    if (env->ExceptionCheck()) {
      env->ExceptionClear();
      Missing(r, c.what);
      continue;
    }
    Expect(r, c.what, x0, c.want_w0);
  }
  env->DeleteLocalRef(cls);
}

// bool AMediaFormat_getInt32(AMediaFormat*, const char*, int32_t*): the same defect on
// a generic NDK proxy trampoline rather than the JNI table.
void ProbeNdkBool(Report& r) {
  AMediaFormat* format = AMediaFormat_new();
  if (format == nullptr) {
    Missing(r, "AMediaFormat_new");
    return;
  }
  int32_t value = 0;
  Expect(r, "AMediaFormat_getInt32, key absent",
         CallRaw(F(&AMediaFormat_getInt32), U(format), U("hello-narrowret.absent"), U(&value), 0), 0);

  AMediaFormat_setInt32(format, "hello-narrowret.present", 7);
  Expect(r, "AMediaFormat_getInt32, key present",
         CallRaw(F(&AMediaFormat_getInt32), U(format), U("hello-narrowret.present"), U(&value), 0), 1);
  AMediaFormat_delete(format);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellonarrowret_MainActivity_probeNarrowReturns(JNIEnv* env, jobject thiz) {
  Report r;
  ProbeExceptionCheck(r, env);
  ProbeIsSameObject(r, env, thiz);
  ProbeStaticCalls(r, env, thiz);
  ProbeNdkBool(r);

  char summary[128];
  if (r.bad == 0) {
    snprintf(summary, sizeof(summary), "narrow-return: all %d checks passed", r.checks);
    LOGI("%s", summary);
  } else {
    snprintf(summary, sizeof(summary), "narrow-return: FAIL (%d of %d checks wrong)", r.bad, r.checks);
    LOGE("%s", summary);
  }
  return env->NewStringUTF((std::string(summary) + "\n\n" + r.text).c_str());
}
