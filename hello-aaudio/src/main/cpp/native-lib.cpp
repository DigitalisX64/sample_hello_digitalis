#include <aaudio/AAudio.h>
#include <android/log.h>
#include <jni.h>

#include <string>

#define LOG_TAG "helloaaudio"

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloaaudio_MainActivity_probeAAudio(JNIEnv* env, jobject /*this*/) {
  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t res = AAudio_createStreamBuilder(&builder);
  std::string msg;
  if (res != AAUDIO_OK || builder == nullptr) {
    msg = std::string("AAudio_createStreamBuilder failed: ") + AAudio_convertResultToText(res);
  } else {
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_delete(builder);
    msg = "libaaudio loaded. AAudio_createStreamBuilder + delete OK.";
  }
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
