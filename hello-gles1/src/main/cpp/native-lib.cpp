#include <GLES/gl.h>
#include <android/log.h>
#include <jni.h>

#include <string>

#define LOG_TAG "hellogles1"

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellogles1_MainActivity_probeGlesV1(JNIEnv* env, jobject /*this*/) {
  // We can't query GL state without an EGL context, but a successful link of
  // libGLESv1_CM and lookup of glGetError proves the proxy library loaded and
  // its symbol table was resolved. glGetError() without a context returns
  // GL_NO_ERROR on most implementations, which is fine for this smoke test.
  GLenum err = glGetError();
  std::string msg = "libGLESv1_CM loaded. glGetError() returned 0x";
  char buf[16];
  snprintf(buf, sizeof(buf), "%x", err);
  msg += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return env->NewStringUTF(msg.c_str());
}
