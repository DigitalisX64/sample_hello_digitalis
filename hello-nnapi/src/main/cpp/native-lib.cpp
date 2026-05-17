#include <android/NeuralNetworks.h>
#include <android/log.h>
#include <jni.h>

#include <string>

#define LOG_TAG "hellonnapi"

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellonnapi_MainActivity_probeNnapi(JNIEnv* env, jobject /*this*/) {
  uint32_t deviceCount = 0;
  int res = ANeuralNetworks_getDeviceCount(&deviceCount);
  std::string msg;
  if (res != ANEURALNETWORKS_NO_ERROR) {
    msg = "ANeuralNetworks_getDeviceCount failed with code " + std::to_string(res);
  } else {
    msg = "libneuralnetworks loaded. Device count: " + std::to_string(deviceCount);
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
