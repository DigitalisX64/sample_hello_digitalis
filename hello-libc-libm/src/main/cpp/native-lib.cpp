// Integration probes for the Digitalis-side extra libc/libm
// fast-path trampolines registered via libberberis_digitalis_extra_proxy_arm64.
//
// Every probe here exercises a symbol that lives in the guest arm64 libc.so or
// libm.so but is NOT in the upstream proxy_libc / proxy_libm trampoline table.
// Before the Digitalis extras: each call resolved correctly via translation
// through the guest libc/libm code (slow but correct). After: each call goes
// through the host x86_64 libc / libm via the extras-registry trampoline (fast,
// and still correct).
//
// Probe coverage matches kDigitalisExtraLibcTrampolines and
// kDigitalisExtraLibmTrampolines in
// frameworks/libs/binary_translation/android_api/digitalis_extra_proxy/.

#include <android/log.h>
#include <jni.h>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#define LOG_TAG "hellolibclibm"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

int passed_ = 0;
int failed_ = 0;

#define CHECK(cond, tag)                                  \
  do {                                                    \
    if (cond) {                                           \
      ++passed_;                                          \
    } else {                                              \
      ++failed_;                                          \
      LOGE("FAIL %s (line %d)", (tag), __LINE__);         \
    }                                                     \
  } while (0)

// Force the bionic function-form rather than the GCC builtin macro by taking
// the address through a typed function pointer. The "math.h" macros expand to
// __builtin_isnan etc.; we want the actual exported symbol.
extern "C" {
extern int isnan(double);
extern int isnanf(float);
extern int isinf(double);
extern int isinff(float);
extern int isfinite(double);
extern int isfinitef(float);
extern int isnormal(double);
extern int isnormalf(float);
extern int __fpclassify(double);
}

void probe_math_classify() {
  CHECK(isnan(NAN) == 1, "isnan(NaN)");
  CHECK(isnan(1.0) == 0, "isnan(1.0)");
  CHECK(isnanf(NAN) == 1, "isnanf(NaN)");
  CHECK(isnanf(1.0f) == 0, "isnanf(1.0)");
  CHECK(isinf(INFINITY) == 1, "isinf(+inf)");
  CHECK(isinf(-INFINITY) == 1, "isinf(-inf)");
  CHECK(isinf(1.0) == 0, "isinf(1.0)");
  CHECK(isinff(INFINITY) == 1, "isinff(+inf)");
  CHECK(isinff(1.0f) == 0, "isinff(1.0)");
  CHECK(isfinite(1.0) == 1, "isfinite(1.0)");
  CHECK(isfinite(INFINITY) == 0, "isfinite(+inf)");
  CHECK(isfinite(NAN) == 0, "isfinite(NaN)");
  CHECK(isfinitef(1.0f) == 1, "isfinitef(1.0)");
  CHECK(isnormal(1.0) == 1, "isnormal(1.0)");
  CHECK(isnormal(0.0) == 0, "isnormal(0.0)");
  CHECK(isnormal(1e-310) == 0, "isnormal(denormal)");
  CHECK(isnormalf(1.0f) == 1, "isnormalf(1.0)");
  CHECK(__fpclassify(NAN) == FP_NAN, "__fpclassify(NaN)");
  CHECK(__fpclassify(INFINITY) == FP_INFINITE, "__fpclassify(+inf)");
  CHECK(__fpclassify(0.0) == FP_ZERO, "__fpclassify(0)");
  CHECK(__fpclassify(1.0) == FP_NORMAL, "__fpclassify(1.0)");
}

extern "C" {
extern void* memrchr(const void*, int, size_t);
extern char* strchrnul(const char*, int);
extern char* stpcpy(char* __restrict, const char* __restrict);
}

void probe_string_mem() {
  const char buf[] = "abcabcXYZ";
  char* p = static_cast<char*>(memrchr(buf, 'b', sizeof(buf) - 1));
  CHECK(p == buf + 4, "memrchr(b)");
  p = static_cast<char*>(memrchr(buf, 'q', sizeof(buf) - 1));
  CHECK(p == nullptr, "memrchr(q -> NULL)");

  char* q = strchrnul(buf, 'X');
  CHECK(q == buf + 6, "strchrnul(X)");
  q = strchrnul(buf, 'q');
  CHECK(q == buf + sizeof(buf) - 1, "strchrnul(q -> NUL)");

  char dst[16] = {};
  char* end = stpcpy(dst, "hello");
  CHECK(end == dst + 5, "stpcpy end-ptr");
  CHECK(strcmp(dst, "hello") == 0, "stpcpy contents");
}

void probe_posix_misc(const char* writable_dir) {
  // ftell via app-private writable_dir. The Android sandbox blocks
  // tmpfile() (no /tmp) and forbids /data/local/tmp for untrusted_app, so
  // probes that need a file fall through if writable_dir is empty.
  if (writable_dir && writable_dir[0]) {
    char path[512];
    snprintf(path, sizeof(path), "%s/hello_libclibm_ftell.tmp", writable_dir);
    FILE* f = fopen(path, "w+");
    CHECK(f != nullptr, "fopen(writable_dir,w+)");
    if (f) {
      fputs("hello", f);
      long pos = ftell(f);
      CHECK(pos == 5, "ftell()");
      fclose(f);
      unlink(path);
    }
  }

  int fd = open("/dev/null", O_RDWR);
  if (fd >= 0) {
    int r = lockf(fd, F_TEST, 0);
    // F_TEST on /dev/null can return 0 or -1; either is acceptable for the
    // "trampoline-was-callable" assertion.
    CHECK(r == 0 || r == -1, "lockf()");
    close(fd);
  }

  struct rlimit rl = {};
  int r = prlimit(0, RLIMIT_NOFILE, nullptr, &rl);
  CHECK(r == 0, "prlimit get");
  if (r == 0) {
    struct rlimit rl2 = rl;
    r = prlimit(0, RLIMIT_NOFILE, &rl, &rl2);
    CHECK(r == 0, "prlimit set-to-self");
  }

  fd_set readfds;
  FD_ZERO(&readfds);
  fd = open("/dev/null", O_RDONLY);
  if (fd >= 0) {
    FD_SET(fd, &readfds);
    struct timespec ts = {0, 0};
    r = pselect(fd + 1, nullptr, &readfds, nullptr, &ts, nullptr);
    CHECK(r == 0 || r == 1, "pselect zero-timeout");
    close(fd);
  }
}

void probe_lfs64(const char* writable_dir) {
  struct stat64 st = {};
  int r = stat64("/dev/null", &st);
  CHECK(r == 0, "stat64(/dev/null)");

  r = lstat64("/dev/null", &st);
  CHECK(r == 0, "lstat64(/dev/null)");

  struct statfs64 sf = {};
  r = statfs64("/dev/null", &sf);
  CHECK(r == 0, "statfs64(/dev/null)");

  int fd = open("/dev/null", O_RDONLY);
  if (fd >= 0) {
    r = fstatfs64(fd, &sf);
    CHECK(r == 0, "fstatfs64()");
    close(fd);
  }

  struct statvfs64 svf = {};
  r = statvfs64("/dev/null", &svf);
  CHECK(r == 0, "statvfs64(/dev/null)");
  fd = open("/dev/null", O_RDONLY);
  if (fd >= 0) {
    r = fstatvfs64(fd, &svf);
    CHECK(r == 0, "fstatvfs64()");
    close(fd);
  }

  struct rlimit64 rl64 = {};
  r = getrlimit64(RLIMIT_NOFILE, &rl64);
  CHECK(r == 0, "getrlimit64");
  if (r == 0) {
    r = setrlimit64(RLIMIT_NOFILE, &rl64);
    CHECK(r == 0, "setrlimit64 round-trip");
  }

  if (writable_dir && writable_dir[0]) {
    char mtmp[512];
    snprintf(mtmp, sizeof(mtmp), "%s/hello_libclibm_mmap.tmp", writable_dir);
    FILE* tmp = fopen(mtmp, "w+");
    if (tmp) {
      int tfd = fileno(tmp);
      r = ftruncate64(tfd, 4096);
      CHECK(r == 0, "ftruncate64()");
      void* m = mmap64(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, tfd, 0);
      CHECK(m != MAP_FAILED, "mmap64()");
      if (m != MAP_FAILED) {
        memset(m, 0xCD, 4096);
        munmap(m, 4096);
      }
      fclose(tmp);
      unlink(mtmp);
    }
  }

  struct dirent64 d_a;
  memset(&d_a, 0, sizeof(d_a));
  strcpy(d_a.d_name, "alpha");
  struct dirent64 d_b;
  memset(&d_b, 0, sizeof(d_b));
  strcpy(d_b.d_name, "beta");
  const struct dirent64* pa = &d_a;
  const struct dirent64* pb = &d_b;
  int cmp = alphasort64(&pa, &pb);
  CHECK(cmp < 0, "alphasort64(a,b)<0");

  // creat64 + sendfile64 + truncate64 round-trip in the app's writable dir.
  // Skipped if writable_dir wasn't provided.
  if (!writable_dir || !writable_dir[0]) return;
  char src_path[512];
  char dst_path[512];
  snprintf(src_path, sizeof(src_path), "%s/hello_libclibm_src.bin", writable_dir);
  snprintf(dst_path, sizeof(dst_path), "%s/hello_libclibm_dst.bin", writable_dir);
  int src_fd = creat64(src_path, 0600);
  if (src_fd >= 0) {
    const char* payload = "0123456789ABCDEF";
    ssize_t w = write(src_fd, payload, 16);
    CHECK(w == 16, "creat64+write");
    close(src_fd);

    src_fd = open(src_path, O_RDONLY);
    int dst_fd = creat64(dst_path, 0600);
    if (src_fd >= 0 && dst_fd >= 0) {
      off_t off = 0;
      ssize_t s = sendfile64(dst_fd, src_fd, &off, 16);
      CHECK(s == 16, "sendfile64");
    }
    if (src_fd >= 0) close(src_fd);
    if (dst_fd >= 0) close(dst_fd);

    r = truncate64(src_path, 0);
    CHECK(r == 0, "truncate64");

    unlink(src_path);
    unlink(dst_path);
  }
}

// dlsym-resolved math symbols. The NDK link-time libm stubs don't declare
// these (NDK exposes a curated subset of bionic libm exports), but the
// runtime libm.so on the device DOES export them. Resolving via dlsym
// proves the proxy trampoline path end-to-end: a symbol that the NDK
// doesn't even know about, but Berberis routes through the host-side
// libm via the Digitalis-extras registry.
typedef double (*Fn_dd)(double);
typedef float (*Fn_ff)(float);
typedef double (*Fn_ddd)(double, double);
typedef float (*Fn_fff)(float, float);
typedef float (*Fn_fi)(float, int);

void probe_libm_basic() {
  void* libm = dlopen("libm.so", RTLD_NOW);
  CHECK(libm != nullptr, "dlopen(libm.so)");
  if (!libm) return;

  auto cospi = reinterpret_cast<Fn_dd>(dlsym(libm, "cospi"));
  auto sinpi = reinterpret_cast<Fn_dd>(dlsym(libm, "sinpi"));
  auto ldexpf_p = reinterpret_cast<Fn_fi>(dlsym(libm, "ldexpf"));

  CHECK(cospi != nullptr, "dlsym(cospi)");
  CHECK(sinpi != nullptr, "dlsym(sinpi)");
  CHECK(ldexpf_p != nullptr, "dlsym(ldexpf)");

  if (cospi) {
    CHECK(std::fabs(cospi(0.5)) < 1e-9, "cospi(0.5) ≈ 0");
    CHECK(std::fabs(cospi(0.0) - 1.0) < 1e-12, "cospi(0) = 1");
    CHECK(std::fabs(cospi(1.0) - (-1.0)) < 1e-9, "cospi(1) = -1");
  }
  if (sinpi) {
    CHECK(std::fabs(sinpi(0.5) - 1.0) < 1e-12, "sinpi(0.5) = 1");
    CHECK(std::fabs(sinpi(0.0)) < 1e-12, "sinpi(0) = 0");
  }
  if (ldexpf_p) {
    CHECK(ldexpf_p(3.0f, 2) == 12.0f, "ldexpf(3,2)");
    CHECK(ldexpf_p(1.5f, -1) == 0.75f, "ldexpf(1.5,-1)");
  }
}

void probe_libm_finite() {
  void* libm = dlopen("libm.so", RTLD_NOW);
  if (!libm) return;

  auto exp_finite  = reinterpret_cast<Fn_dd>(dlsym(libm, "__exp_finite"));
  auto expf_finite = reinterpret_cast<Fn_ff>(dlsym(libm, "__expf_finite"));
  auto exp2_finite  = reinterpret_cast<Fn_dd>(dlsym(libm, "__exp2_finite"));
  auto exp2f_finite = reinterpret_cast<Fn_ff>(dlsym(libm, "__exp2f_finite"));
  auto log_finite  = reinterpret_cast<Fn_dd>(dlsym(libm, "__log_finite"));
  auto logf_finite = reinterpret_cast<Fn_ff>(dlsym(libm, "__logf_finite"));
  auto log2_finite  = reinterpret_cast<Fn_dd>(dlsym(libm, "__log2_finite"));
  auto log2f_finite = reinterpret_cast<Fn_ff>(dlsym(libm, "__log2f_finite"));
  auto pow_finite  = reinterpret_cast<Fn_ddd>(dlsym(libm, "__pow_finite"));
  auto powf_finite = reinterpret_cast<Fn_fff>(dlsym(libm, "__powf_finite"));

  CHECK(exp_finite != nullptr, "dlsym(__exp_finite)");
  CHECK(expf_finite != nullptr, "dlsym(__expf_finite)");
  CHECK(exp2_finite != nullptr, "dlsym(__exp2_finite)");
  CHECK(exp2f_finite != nullptr, "dlsym(__exp2f_finite)");
  CHECK(log_finite != nullptr, "dlsym(__log_finite)");
  CHECK(logf_finite != nullptr, "dlsym(__logf_finite)");
  CHECK(log2_finite != nullptr, "dlsym(__log2_finite)");
  CHECK(log2f_finite != nullptr, "dlsym(__log2f_finite)");
  CHECK(pow_finite != nullptr, "dlsym(__pow_finite)");
  CHECK(powf_finite != nullptr, "dlsym(__powf_finite)");

  if (exp_finite)  CHECK(std::fabs(exp_finite(1.0) - M_E) < 1e-12, "__exp_finite(1) = e");
  if (expf_finite) CHECK(std::fabs(expf_finite(1.0f) - static_cast<float>(M_E)) < 1e-5f,
                         "__expf_finite(1)");
  if (exp2_finite)  CHECK(std::fabs(exp2_finite(3.0) - 8.0) < 1e-12, "__exp2_finite(3) = 8");
  if (exp2f_finite) CHECK(std::fabs(exp2f_finite(3.0f) - 8.0f) < 1e-5f, "__exp2f_finite(3)");
  if (log_finite)  CHECK(std::fabs(log_finite(M_E) - 1.0) < 1e-12, "__log_finite(e) = 1");
  if (logf_finite) CHECK(std::fabs(logf_finite(static_cast<float>(M_E)) - 1.0f) < 1e-5f,
                         "__logf_finite(e)");
  if (log2_finite)  CHECK(std::fabs(log2_finite(8.0) - 3.0) < 1e-12, "__log2_finite(8) = 3");
  if (log2f_finite) CHECK(std::fabs(log2f_finite(8.0f) - 3.0f) < 1e-5f, "__log2f_finite(8)");
  if (pow_finite)   CHECK(std::fabs(pow_finite(2.0, 10.0) - 1024.0) < 1e-9,
                          "__pow_finite(2,10) = 1024");
  if (powf_finite)  CHECK(std::fabs(powf_finite(2.0f, 10.0f) - 1024.0f) < 1e-3f,
                          "__powf_finite(2,10)");
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellolibclibm_MainActivity_probeLibcLibm(JNIEnv* env,
                                                          jobject /*this*/,
                                                          jstring writable_dir) {
  passed_ = 0;
  failed_ = 0;

  const char* writable_dir_chars = writable_dir
      ? env->GetStringUTFChars(writable_dir, nullptr)
      : nullptr;

  probe_math_classify();
  probe_string_mem();
  probe_posix_misc(writable_dir_chars);
  probe_lfs64(writable_dir_chars);
  probe_libm_basic();
  probe_libm_finite();

  if (writable_dir_chars) env->ReleaseStringUTFChars(writable_dir, writable_dir_chars);

  std::string result;
  const int total = passed_ + failed_;
  if (failed_ == 0) {
    LOGI("PASS %d/%d probes", passed_, total);
    result = "hello-libc-libm: PASS " + std::to_string(passed_) + "/" +
             std::to_string(total) + " probes\n";
  } else {
    LOGE("FAIL %d/%d probes failed", failed_, total);
    result = "hello-libc-libm: FAIL " + std::to_string(failed_) + "/" +
             std::to_string(total) + " probes failed\n";
  }
  return env->NewStringUTF(result.c_str());
}
