#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <jni.h>

#include <cstdint>
#include <string>

#define LOG_TAG "hellojnihelp"

namespace {

// Function-pointer types for the 12 libnativehelper jni* helpers that are
// DoBadTrampoline in the upstream proxy and covered in-surface by Digitalis
// (digitalis_extra_libnativehelper_trampolines.cc). Each takes a JNIEnv* first
// argument, so the Digitalis trampoline must translate it with ToHostJNIEnv; a
// missing trampoline would abort the process with "Bad '<sym>' call". The guest
// JNIEnv* is the same pointer value the C JNIHelp API calls C_JNIEnv*, so we
// declare the leading argument as JNIEnv* throughout.
using ThrowExceptionFn = int (*)(JNIEnv*, const char*, const char*);
using ThrowMsgFn = int (*)(JNIEnv*, const char*);
using ThrowIOFn = int (*)(JNIEnv*, int);
using ThrowErrnoFn = int (*)(JNIEnv*, const char*, int);
using LogExceptionFn = void (*)(JNIEnv*, int, const char*, jthrowable);
using CreateStringFn = jstring (*)(JNIEnv*, const jchar*, jsize);
using NioFieldsFn = jlong (*)(JNIEnv*, jobject, jint*, jint*, jint*);
using NioPointerFn = jlong (*)(JNIEnv*, jobject);
using NioBaseArrayFn = jarray (*)(JNIEnv*, jobject);
using NioBaseArrayOffsetFn = jint (*)(JNIEnv*, jobject);
using RegisterNativeMethodsFn = jint (*)(JNIEnv*, const char*, const JNINativeMethod*, jint);

// Native impl rebound onto MainActivity.nativeRegisteredProbe via the
// jniRegisterNativeMethods trampoline probe below.
jint RegisteredProbe(JNIEnv* /*env*/, jobject /*thiz*/) { return 4242; }

// Backing storage for the direct ByteBuffer used to drive the NIO-buffer
// helpers; kept static so the address stays valid for the buffer's lifetime.
alignas(8) uint8_t g_direct_buf[64];

// Drives all 12 covered libnativehelper trampolines and self-checks each. A
// covered trampoline that regressed (lost its registration) would abort the
// process here; a trampoline that returns the wrong value makes this log "FAIL",
// which the StatusTestRule treats as a failure. On success it logs "jnihelp OK".
std::string Probe(JNIEnv* env) {
  void* h = dlopen("libnativehelper.so", RTLD_NOW);
  if (h == nullptr) {
    return std::string("FAIL: dlopen libnativehelper.so failed: ") + dlerror();
  }

  auto throw_exc = reinterpret_cast<ThrowExceptionFn>(dlsym(h, "jniThrowException"));
  auto throw_npe = reinterpret_cast<ThrowMsgFn>(dlsym(h, "jniThrowNullPointerException"));
  auto throw_rte = reinterpret_cast<ThrowMsgFn>(dlsym(h, "jniThrowRuntimeException"));
  auto throw_io = reinterpret_cast<ThrowIOFn>(dlsym(h, "jniThrowIOException"));
  auto throw_errno = reinterpret_cast<ThrowErrnoFn>(dlsym(h, "jniThrowErrnoException"));
  auto log_exc = reinterpret_cast<LogExceptionFn>(dlsym(h, "jniLogException"));
  auto create_str = reinterpret_cast<CreateStringFn>(dlsym(h, "jniCreateString"));
  auto nio_fields = reinterpret_cast<NioFieldsFn>(dlsym(h, "jniGetNioBufferFields"));
  auto nio_ptr = reinterpret_cast<NioPointerFn>(dlsym(h, "jniGetNioBufferPointer"));
  auto nio_base = reinterpret_cast<NioBaseArrayFn>(dlsym(h, "jniGetNioBufferBaseArray"));
  auto nio_base_off =
      reinterpret_cast<NioBaseArrayOffsetFn>(dlsym(h, "jniGetNioBufferBaseArrayOffset"));
  auto register_natives =
      reinterpret_cast<RegisterNativeMethodsFn>(dlsym(h, "jniRegisterNativeMethods"));

  if (!throw_exc || !throw_npe || !throw_rte || !throw_io || !throw_errno || !log_exc ||
      !create_str || !nio_fields || !nio_ptr || !nio_base || !nio_base_off || !register_natives) {
    return "FAIL: dlsym of a libnativehelper jni* helper returned null";
  }

  std::string fails;
  auto expect = [&](bool ok, const char* what) {
    if (!ok) {
      fails += " ";
      fails += what;
    }
  };

  // (1) jniThrowException: sets a pending exception; must leave one pending.
  throw_exc(env, "java/lang/RuntimeException", "jnihelp test");
  expect(env->ExceptionCheck() == JNI_TRUE, "jniThrowException(no-pending)");
  env->ExceptionClear();

  // (2) jniThrowNullPointerException.
  throw_npe(env, "jnihelp npe");
  expect(env->ExceptionCheck() == JNI_TRUE, "jniThrowNullPointerException(no-pending)");
  env->ExceptionClear();

  // (3) jniThrowRuntimeException — capture the throwable for jniLogException (6).
  throw_rte(env, "jnihelp rte");
  bool rte_pending = env->ExceptionCheck() == JNI_TRUE;
  expect(rte_pending, "jniThrowRuntimeException(no-pending)");
  jthrowable captured = rte_pending ? env->ExceptionOccurred() : nullptr;
  env->ExceptionClear();

  // (4) jniThrowIOException.
  throw_io(env, EACCES);
  expect(env->ExceptionCheck() == JNI_TRUE, "jniThrowIOException(no-pending)");
  env->ExceptionClear();

  // (5) jniThrowErrnoException.
  throw_errno(env, "jnihelp_fn", EACCES);
  expect(env->ExceptionCheck() == JNI_TRUE, "jniThrowErrnoException(no-pending)");
  env->ExceptionClear();

  // (6) jniLogException — logs the captured throwable (no pending exception now).
  if (captured != nullptr) {
    log_exc(env, ANDROID_LOG_WARN, LOG_TAG, captured);
    env->DeleteLocalRef(captured);
  }
  if (env->ExceptionCheck()) env->ExceptionClear();

  // (7) jniCreateString: build a jstring from a jchar array; verify length.
  const jchar chars[] = {'D', 'i', 'g', 'i', 't', 'a', 'l', 'i', 's'};
  const jsize nchars = sizeof(chars) / sizeof(chars[0]);
  jstring s = create_str(env, chars, nchars);
  expect(s != nullptr && env->GetStringLength(s) == nchars, "jniCreateString(len)");
  if (s != nullptr) env->DeleteLocalRef(s);

  // (8)-(11) NIO-buffer helpers on a direct ByteBuffer whose base we control, so
  // the returned pointer/fields can be checked against ground truth.
  jobject dbb = env->NewDirectByteBuffer(g_direct_buf, sizeof(g_direct_buf));
  const jlong want_addr = static_cast<jlong>(reinterpret_cast<uintptr_t>(g_direct_buf));
  if (dbb == nullptr) {
    expect(false, "NewDirectByteBuffer(null)");
  } else {
    jlong ptr = nio_ptr(env, dbb);
    expect(ptr == want_addr, "jniGetNioBufferPointer(addr)");

    jint pos = -1, limit = -1, shift = -1;
    jlong base = nio_fields(env, dbb, &pos, &limit, &shift);
    expect(base == want_addr && pos == 0 && limit == static_cast<jint>(sizeof(g_direct_buf)) &&
               shift == 0,
           "jniGetNioBufferFields(fields)");

    // Direct buffer has no backing array: base array is null, offset 0. These
    // must RETURN (not abort) — the values themselves are informational.
    (void)nio_base(env, dbb);
    if (env->ExceptionCheck()) env->ExceptionClear();
    (void)nio_base_off(env, dbb);
    if (env->ExceptionCheck()) env->ExceptionClear();

    env->DeleteLocalRef(dbb);
  }

  // (12) jniRegisterNativeMethods: rebind MainActivity.nativeRegisteredProbe to
  // RegisteredProbe and confirm JNI_OK.
  const JNINativeMethod methods[] = {
      {"nativeRegisteredProbe", "()I", reinterpret_cast<void*>(&RegisteredProbe)},
  };
  jint rc = register_natives(env, "com/example/hellojnihelp/MainActivity", methods, 1);
  expect(rc == JNI_OK, "jniRegisterNativeMethods(rc)");
  if (env->ExceptionCheck()) env->ExceptionClear();

  if (!fails.empty()) {
    return "FAIL: libnativehelper helper(s):" + fails;
  }
  return "jnihelp OK: all 12 libnativehelper jni* trampolines verified";
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellojnihelp_MainActivity_probeJniHelp(JNIEnv* env, jobject /*this*/) {
  std::string msg = Probe(env);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
