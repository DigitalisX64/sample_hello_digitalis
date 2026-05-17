#include <android/binder_ibinder.h>
#include <android/binder_status.h>
#include <android/log.h>
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
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
