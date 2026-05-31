#include <android/binder_ibinder.h>
#include <android/binder_status.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>

#include <string>

#define LOG_TAG "hellobinderndk"

namespace {

void* OnCreate(void* args) { return args; }
void OnDestroy(void* /*userData*/) {}
binder_status_t OnTransact(AIBinder* /*binder*/, transaction_code_t /*code*/,
                           const AParcel* /*in*/, AParcel* /*out*/) {
  return STATUS_OK;
}

// AServiceManager_* live in libbinder_ndk but their header (binder_manager.h)
// is system/LLNDK, not in the public NDK sysroot, so we reach the symbols via
// dlsym rather than linking. This also routes the calls through the proxy
// trampoline exactly as a guest call would. The two symbols below are
// DoBadTrampoline in the upstream table; the Digitalis trampolines cover them.
struct AServiceManager_NotificationRegistration;
using OnRegisterFn = void (*)(const char* instance, AIBinder* registered, void* cookie);
using RegisterFn = AServiceManager_NotificationRegistration* (*)(const char* instance,
                                                                 OnRegisterFn onRegister,
                                                                 void* cookie);
using DeleteFn = void (*)(AServiceManager_NotificationRegistration*);

// Host->guest callback; the host may invoke it on a binder thread. Reaching it
// at all proves the Digitalis trampoline wrapped this guest function (via
// WrapGuestFunction) and dispatched back into the guest.
void OnServiceRegister(const char* instance, AIBinder* /*registered*/, void* /*cookie*/) {
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "onRegister fired for %s",
                      instance ? instance : "(null)");
}

// Exercises AServiceManager_registerForServiceNotifications (host<-guest
// callback wrapped by the Digitalis trampoline) and
// AServiceManager_NotificationRegistration_delete (opaque-handle pass-through).
// Without the Digitalis coverage either call aborts with "Bad '<sym>' call".
const char* ProbeServiceNotifications() {
  void* handle = dlopen("libbinder_ndk.so", RTLD_NOW);
  if (handle == nullptr) {
    return "svc-notif: dlopen libbinder_ndk failed";
  }
  auto reg_fn = reinterpret_cast<RegisterFn>(
      dlsym(handle, "AServiceManager_registerForServiceNotifications"));
  auto del_fn =
      reinterpret_cast<DeleteFn>(dlsym(handle, "AServiceManager_NotificationRegistration_delete"));
  if (reg_fn == nullptr || del_fn == nullptr) {
    return "svc-notif: dlsym failed";
  }
  AServiceManager_NotificationRegistration* reg =
      reg_fn("android.hardware.power.IPower/default", OnServiceRegister, nullptr);
  if (reg == nullptr) {
    return "svc-notif: register returned null";
  }
  del_fn(reg);
  return "svc-notif: register+delete OK";
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellobinderndk_MainActivity_probeBinderNdk(JNIEnv* env, jobject /*this*/) {
  AIBinder_Class* clazz =
      AIBinder_Class_define("com.example.hellodigitalis.NoOp", OnCreate, OnDestroy, OnTransact);
  std::string msg;
  if (clazz == nullptr) {
    msg = "AIBinder_Class_define returned null";
  } else {
    AIBinder* binder = AIBinder_new(clazz, /*args=*/nullptr);
    if (binder == nullptr) {
      msg = "AIBinder_new returned null";
    } else {
      AIBinder_decStrong(binder);
      msg = "libbinder_ndk loaded. AIBinder_new + decStrong OK.";
    }
  }
  msg += " | ";
  msg += ProbeServiceNotifications();
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
