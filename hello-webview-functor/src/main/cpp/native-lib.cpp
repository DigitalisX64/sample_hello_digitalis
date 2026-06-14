#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <string>

#define LOG_TAG "hellowebviewfunctor"

namespace {

using RegisterFn = jint (*)(JNIEnv*);
using CreateFn = long (*)(int, int);
using GetStrideStaticFn = int (*)(long);
using ReleaseFn = void (*)(long);

// Proxy-smoke test for libwebviewchromium_plat_support (the WebView hardware-
// accel support library). Every symbol is DoBadTrampoline in the upstream proxy,
// so without the Digitalis coverage each call below aborts the process with
// "Bad '<sym>' call". The three Register*(JNIEnv*) entry points also exercise the
// host-VM-attach path (the host registration needs a valid host JNIEnv). The
// point is that the calls RETURN (success or a benign error) instead of aborting;
// FindClass for the WebView delegate class may fail from this app's context,
// which is fine.
std::string Probe(JNIEnv* env) {
  void* h = dlopen("libwebviewchromium_plat_support.so", RTLD_NOW);
  if (h == nullptr) {
    return std::string("dlopen libwebviewchromium_plat_support failed: ") + dlerror();
  }
  auto reg_draw =
      reinterpret_cast<RegisterFn>(dlsym(h, "_ZN7android19RegisterDrawFunctorEP7_JNIEnv"));
  auto reg_gl =
      reinterpret_cast<RegisterFn>(dlsym(h, "_ZN7android21RegisterDrawGLFunctorEP7_JNIEnv"));
  auto reg_gu =
      reinterpret_cast<RegisterFn>(dlsym(h, "_ZN7android21RegisterGraphicsUtilsEP7_JNIEnv"));
  auto create = reinterpret_cast<CreateFn>(dlsym(h, "_ZN7android17GraphicBufferImpl6CreateEii"));
  auto stride = reinterpret_cast<GetStrideStaticFn>(
      dlsym(h, "_ZN7android17GraphicBufferImpl15GetStrideStaticEl"));
  auto release =
      reinterpret_cast<ReleaseFn>(dlsym(h, "_ZN7android17GraphicBufferImpl7ReleaseEl"));
  if (reg_draw == nullptr || reg_gl == nullptr || reg_gu == nullptr || create == nullptr ||
      stride == nullptr || release == nullptr) {
    return "dlsym of a webview plat_support symbol failed";
  }

  // JNI registration entry points. Clear any pending exception the host FindClass
  // may leave (the WebView delegate class is not resolvable from this app's class
  // loader) so the JNI calls that follow do not abort.
  jint r1 = reg_draw(env);
  if (env->ExceptionCheck()) env->ExceptionClear();
  jint r2 = reg_gl(env);
  if (env->ExceptionCheck()) env->ExceptionClear();
  jint r3 = reg_gu(env);
  if (env->ExceptionCheck()) env->ExceptionClear();

  // GraphicBufferImpl flat-marshalled static path. Create may fail on the
  // emulator gralloc (returns 0); either way it returns without aborting.
  long id = create(64, 64);
  int st = (id != 0) ? stride(id) : -1;
  if (id != 0) {
    release(id);
  }

  return "webview-functor OK: RegisterDrawFunctor=" + std::to_string(r1) +
         " DrawGL=" + std::to_string(r2) + " GraphicsUtils=" + std::to_string(r3) +
         " GraphicBuffer(id=" + std::to_string(id) + ",stride=" + std::to_string(st) + ")";
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellowebviewfunctor_MainActivity_probeWebViewFunctor(JNIEnv* env,
                                                                      jobject /*this*/) {
  std::string msg = Probe(env);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
