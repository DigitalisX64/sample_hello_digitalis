#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <cstring>
#include <string>

#define LOG_TAG "hellowebviewfunctor"

namespace {

// libwebviewchromium_plat_support exports 18 symbols; 17 are DoBadTrampoline in
// the upstream proxy and are covered by the Digitalis extra trampolines
// (digitalis_extra_libwebviewchromium_plat_support_trampolines.cc). This probe
// drives ALL 17 so a regression (an unregistered/removed trampoline) is caught:
// any covered symbol that lost its trampoline would abort the process with
// "Bad '<sym>' call" (caught by StatusTestRule as "Fatal signal"). The only
// uncovered symbol, JNI_OnLoad, is the proxy lib's own load-time entry and is
// deliberately not exercised here (see the §13 coverage inventory in digitalis/docs/how-it-works.md).

// AwMapMode (android_webview/public/browser/draw_fn.h): READ_ONLY=0,
// WRITE_ONLY=1, READ_WRITE=2.
constexpr int kAwMapReadWrite = 2;

// Register{DrawFunctor,DrawGLFunctor,GraphicsUtils}(JNIEnv*).
using RegisterFn = jint (*)(JNIEnv*);
// android::RaiseFileNumberLimit().
using RaiseFn = void (*)();
// GraphicBufferImpl static methods (buffer-id is a host-registry key, opaque).
using CreateFn = long (*)(int, int);
using ReleaseFn = void (*)(long);
using GetStrideStaticFn = int (*)(long);
using GetNativeBufferStaticFn = void* (*)(long);
using MapStaticFn = int (*)(long, int /*AwMapMode*/, void** /*vaddr*/);
using UnmapStaticFn = int (*)(long);
// GraphicBufferImpl instance methods (leading void* is `this`).
using CtorFn = void (*)(void* /*this*/, unsigned, unsigned);
using DtorFn = void (*)(void* /*this*/);
using InitCheckFn = int (*)(void* /*this*/);
using GetStrideFn = int (*)(void* /*this*/);
using GetNativeBufferFn = void* (*)(void* /*this*/);
using MapFn = int (*)(void* /*this*/, int /*AwMapMode*/, void** /*vaddr*/);
using UnmapFn = int (*)(void* /*this*/);

template <typename T>
T Sym(void* h, const char* name) {
  return reinterpret_cast<T>(dlsym(h, name));
}

std::string Probe(JNIEnv* env) {
  void* h = dlopen("libwebviewchromium_plat_support.so", RTLD_NOW);
  if (h == nullptr) {
    return std::string("dlopen libwebviewchromium_plat_support failed: ") + dlerror();
  }

  auto reg_draw = Sym<RegisterFn>(h, "_ZN7android19RegisterDrawFunctorEP7_JNIEnv");
  auto reg_gl = Sym<RegisterFn>(h, "_ZN7android21RegisterDrawGLFunctorEP7_JNIEnv");
  auto reg_gu = Sym<RegisterFn>(h, "_ZN7android21RegisterGraphicsUtilsEP7_JNIEnv");
  auto raise = Sym<RaiseFn>(h, "_ZN7android20RaiseFileNumberLimitEv");
  auto create = Sym<CreateFn>(h, "_ZN7android17GraphicBufferImpl6CreateEii");
  auto release = Sym<ReleaseFn>(h, "_ZN7android17GraphicBufferImpl7ReleaseEl");
  auto stride_s = Sym<GetStrideStaticFn>(h, "_ZN7android17GraphicBufferImpl15GetStrideStaticEl");
  auto nbuf_s =
      Sym<GetNativeBufferStaticFn>(h, "_ZN7android17GraphicBufferImpl21GetNativeBufferStaticEl");
  auto map_s = Sym<MapStaticFn>(h, "_ZN7android17GraphicBufferImpl9MapStaticEl9AwMapModePPv");
  auto unmap_s = Sym<UnmapStaticFn>(h, "_ZN7android17GraphicBufferImpl11UnmapStaticEl");
  auto ctor = Sym<CtorFn>(h, "_ZN7android17GraphicBufferImplC2Ejj");
  auto dtor = Sym<DtorFn>(h, "_ZN7android17GraphicBufferImplD2Ev");
  auto init_check = Sym<InitCheckFn>(h, "_ZNK7android17GraphicBufferImpl9InitCheckEv");
  auto stride_i = Sym<GetStrideFn>(h, "_ZNK7android17GraphicBufferImpl9GetStrideEv");
  auto nbuf_i = Sym<GetNativeBufferFn>(h, "_ZNK7android17GraphicBufferImpl15GetNativeBufferEv");
  auto map_i = Sym<MapFn>(h, "_ZN7android17GraphicBufferImpl3MapE9AwMapModePPv");
  auto unmap_i = Sym<UnmapFn>(h, "_ZN7android17GraphicBufferImpl5UnmapEv");

  if (!reg_draw || !reg_gl || !reg_gu || !raise || !create || !release || !stride_s || !nbuf_s ||
      !map_s || !unmap_s || !ctor || !dtor || !init_check || !stride_i || !nbuf_i || !map_i ||
      !unmap_i) {
    return "FAIL: dlsym of a webview plat_support symbol failed";
  }

  std::string fail;

  // --- The three JNI registration entry points. They run
  // jniRegisterNativeMethods on the host VM; from this app's worker thread the
  // host env is unavailable so they return -1 (the trampoline forwarded without
  // aborting, which is what we verify). The host FindClass for the WebView
  // delegate class may leave a pending exception; clear it.
  jint r1 = reg_draw(env);
  if (env->ExceptionCheck()) env->ExceptionClear();
  jint r2 = reg_gl(env);
  if (env->ExceptionCheck()) env->ExceptionClear();
  jint r3 = reg_gu(env);
  if (env->ExceptionCheck()) env->ExceptionClear();

  // --- android::RaiseFileNumberLimit(): no args, must return without aborting.
  raise();

  // --- GraphicBufferImpl static path (buffer-id keyed; the emulator gralloc
  // returns a valid non-zero id, so the full id-based surface is exercisable).
  long id = create(64, 64);
  int s_stride = -1, s_map = -1, s_unmap = -1;
  void* s_nb = nullptr;
  void* s_va = nullptr;
  if (id != 0) {
    s_stride = stride_s(id);
    s_nb = nbuf_s(id);
    s_map = map_s(id, kAwMapReadWrite, &s_va);
    s_unmap = unmap_s(id);
    release(id);
  } else {
    fail += "[static Create returned 0]";
  }

  // --- GraphicBufferImpl instance path. Construct in an over-allocated, zeroed
  // buffer via the C2 ctor (the object is small; 512 zeroed bytes is a safe
  // upper bound), drive every instance method, then run the D2 dtor. Map/Unmap
  // only when the underlying buffer constructed OK (InitCheck == 0) to avoid
  // locking a failed buffer.
  alignas(16) unsigned char obj[512];
  std::memset(obj, 0, sizeof(obj));
  ctor(obj, 64, 64);
  int i_check = init_check(obj);
  int i_stride = stride_i(obj);
  void* i_nb = nbuf_i(obj);
  int i_map = -1, i_unmap = -1;
  void* i_va = nullptr;
  if (i_check == 0) {
    i_map = map_i(obj, kAwMapReadWrite, &i_va);
    i_unmap = unmap_i(obj);
  }
  dtor(obj);

  // Ground-truth self-checks (beyond no-abort): the static path produced a
  // valid buffer with a positive stride and mappable CPU address; the instance
  // path constructed an object whose accessors returned consistent values.
  if (id != 0) {
    if (s_stride <= 0) fail += "[GetStrideStatic<=0]";
    if (s_nb == nullptr) fail += "[GetNativeBufferStatic null]";
    if (s_map != 0 || s_va == nullptr) fail += "[MapStatic failed]";
    if (s_unmap != 0) fail += "[UnmapStatic failed]";
  }
  if (i_check == 0) {
    if (i_stride <= 0) fail += "[instance GetStride<=0]";
    if (i_nb == nullptr) fail += "[instance GetNativeBuffer null]";
    if (i_map != 0 || i_va == nullptr) fail += "[instance Map failed]";
    if (i_unmap != 0) fail += "[instance Unmap failed]";
  }

  std::string status = fail.empty() ? "webview-functor OK: all 17 trampolines driven"
                                    : ("FAIL: " + fail);
  status += " | Register(draw=" + std::to_string(r1) + ",gl=" + std::to_string(r2) +
            ",gu=" + std::to_string(r3) + ")" + " static(id=" + std::to_string(id) +
            ",stride=" + std::to_string(s_stride) + ",map=" + std::to_string(s_map) + ")" +
            " inst(check=" + std::to_string(i_check) + ",stride=" + std::to_string(i_stride) +
            ",map=" + std::to_string(i_map) + ")";
  return status;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellowebviewfunctor_MainActivity_probeWebViewFunctor(JNIEnv* env,
                                                                      jobject /*this*/) {
  std::string msg = Probe(env);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
