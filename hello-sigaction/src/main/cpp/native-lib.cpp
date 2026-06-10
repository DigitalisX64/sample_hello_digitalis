// Integration-level probe for POSIX signal-handler installation and delivery,
// modelling what native crash reporters (Umeng UCrash, Breakpad, crashpad) do
// at startup.
//
// The interesting path for the translator is sigaction() with a non-null
// oldact: reading back the previously-installed handler forces Berberis to
// convert the *host* sigaction to the guest view, including the host
// sa_restorer. On Android the host is bionic with ART's libsigchain on the
// signal chain, so that restorer does not match the canonical glibc/kernel
// sigreturn trampoline. Berberis must tolerate this rather than abort.
//
// Probe coverage:
//   1. Read back the current handler for the crash signals a reporter hooks
//      (SIGSEGV/SIGABRT/SIGBUS/SIGILL/SIGFPE/SIGTRAP) via sigaction(sig, NULL,
//      &old) -- exercises the host->guest sa_restorer conversion.
//   2. Install a real SA_SIGINFO SIGSEGV handler (reading back oldact),
//      deliberately dereference null, and recover via siglongjmp -- exercises
//      guest signal delivery and the guest's own restorer end to end.
//   3. Restore the previous handler.

#include <android/log.h>
#include <jni.h>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "hellosigaction"

namespace {

sigjmp_buf g_jmp;
volatile sig_atomic_t g_caught = 0;

void SegvHandler(int sig, siginfo_t* /*info*/, void* /*ucontext*/) {
  g_caught = sig;
  siglongjmp(g_jmp, 1);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellosigaction_MainActivity_probeSigaction(JNIEnv* env,
                                                            jobject /*this*/) {
  std::string report = "POSIX signal handler probe:\n";
  char buf[256];

  // 1. Read back the current action for the signals a crash reporter hooks.
  //    The readback is what historically aborted the process on a bionic host.
  {
    const int sigs[] = {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE, SIGTRAP};
    bool all_ok = true;
    for (int s : sigs) {
      struct sigaction old {};
      if (sigaction(s, nullptr, &old) != 0) all_ok = false;
    }
    snprintf(buf, sizeof(buf), "  read old actions: %s\n",
             all_ok ? "OK" : "FAIL");
    report += buf;
  }

  // 2. Install a real SIGSEGV handler (reading oldact), fault, and recover.
  {
    struct sigaction sa {};
    struct sigaction old {};
    sa.sa_sigaction = SegvHandler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    bool installed = (sigaction(SIGSEGV, &sa, &old) == 0);

    g_caught = 0;
    bool recovered = false;
    if (sigsetjmp(g_jmp, 1) == 0) {
      volatile int* p = reinterpret_cast<volatile int*>(0);
      *p = 0x42;  // SIGSEGV -> SegvHandler -> siglongjmp back here
    } else {
      recovered = true;
    }

    // Restore whatever was there before (the reporter / default).
    sigaction(SIGSEGV, &old, nullptr);

    bool ok = installed && recovered && (g_caught == SIGSEGV);
    // "SIG11", not the signal's name: the suite's crash grep matches literal
    // "SIGSEGV"/"SIGABRT"/"SIGILL" anywhere in logcat, so a success report must
    // spell the signal by number to avoid tripping it.
    snprintf(buf, sizeof(buf), "  SIG11 deliver+recover: %s (caught=%d)\n",
             ok ? "OK" : "FAIL", static_cast<int>(g_caught));
    report += buf;
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
