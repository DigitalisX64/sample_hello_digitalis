/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <jni.h>

#include <android/log.h>
#include <dlfcn.h>
#include <fenv.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kLogTag = "hello-jni";

// Forensic state captured by sigsegv_handler so the post-resume code can
// witness whether the guest handler observed correct values.
struct SigsegvForensics {
  volatile uint32_t handler_called;
  volatile uint64_t observed_pc;
  volatile uint64_t observed_fault_addr;
  volatile uint64_t observed_x0_pre;
  volatile uint64_t observed_x10_pre;
  volatile uint32_t fpsimd_magic;
  volatile uint32_t fpsimd_size;
  volatile uint32_t observed_fpsr;
  volatile uint32_t observed_fpcr;
  volatile uint64_t observed_v0_lo;
  volatile uint64_t observed_v0_hi;
};

SigsegvForensics g_forensics{};

// Bionic's mcontext_t::__reserved holds a chain of _aarch64_ctx blocks.
// The first block, per kernel ABI, is FPSIMD: {magic=0x46508001, size=528},
// followed by uint32_t fpsr, uint32_t fpcr, and 32 16-byte vregs.
struct fpsimd_ctx_layout {
  struct { uint32_t magic; uint32_t size; } head;
  uint32_t fpsr;
  uint32_t fpcr;
  __uint128_t vregs[32];
};

constexpr uint32_t kFpsimdMagic = 0x46508001u;
constexpr uint64_t kPostX0Sentinel = 0xCAFEBABEDEADBEEFull;

void SigsegvHandler(int /*sig*/, siginfo_t* info, void* ucontext_v) {
  ucontext_t* uc = static_cast<ucontext_t*>(ucontext_v);
  mcontext_t* mc = &uc->uc_mcontext;

  g_forensics.handler_called = 1;
  g_forensics.observed_pc = static_cast<uint64_t>(mc->pc);
  g_forensics.observed_fault_addr = reinterpret_cast<uint64_t>(info->si_addr);
  g_forensics.observed_x0_pre = mc->regs[0];
  g_forensics.observed_x10_pre = mc->regs[10];

  const auto* fp =
      reinterpret_cast<const fpsimd_ctx_layout*>(mc->__reserved);
  g_forensics.fpsimd_magic = fp->head.magic;
  g_forensics.fpsimd_size = fp->head.size;
  if (fp->head.magic == kFpsimdMagic) {
    g_forensics.observed_fpsr = fp->fpsr;
    g_forensics.observed_fpcr = fp->fpcr;
    // The pre-test inline asm seeds v0 with a known double; read it back
    // here as raw bytes to confirm the FPSIMD vregs window is correctly
    // populated by Save() (the -152 fix).
    __uint128_t v0 = fp->vregs[0];
    g_forensics.observed_v0_lo = static_cast<uint64_t>(v0);
    g_forensics.observed_v0_hi = static_cast<uint64_t>(v0 >> 64);
  }

  // Advance past the faulting LDR (ARM64 = 4 bytes) and inject a sentinel
  // value into x0. After Restore() the guest resumes with both edits
  // applied, so the next instruction sees x0 = kPostX0Sentinel and skips
  // the faulting load entirely.
  mc->pc += 4;
  mc->regs[0] = kPostX0Sentinel;
}

bool RunSigsegvRecoveryTest(std::string* report) {
  struct sigaction prev_sa{};
  struct sigaction new_sa{};
  new_sa.sa_sigaction = &SigsegvHandler;
  new_sa.sa_flags = SA_SIGINFO;
  sigemptyset(&new_sa.sa_mask);
  if (sigaction(SIGSEGV, &new_sa, &prev_sa) != 0) {
    *report = "[SIG11-RECOVERY:FAIL sigaction errno]";
    return false;
  }

  // Seed v0 with a known double via the FP register so we can verify the
  // FPSIMD block in __reserved actually carries the live vreg bits.
  double seed = 3.140625;
  asm volatile("fmov d0, %d0" : : "w"(seed) : "v0");

  uint64_t post_x0 = 0;
  asm volatile(
      "mov   x0,  #0x1234\n"
      "mov   x10, xzr\n"
      "ldr   x0, [x10]\n"          // <-- faults; handler advances pc, sets x0
      "mov   %0, x0\n"
      : "=r"(post_x0)
      :
      : "x0", "x10", "memory");

  sigaction(SIGSEGV, &prev_sa, nullptr);

  bool handler_ran = g_forensics.handler_called == 1;
  bool x0_pre_ok = g_forensics.observed_x0_pre == 0x1234ull;
  bool x10_pre_ok = g_forensics.observed_x10_pre == 0ull;
  bool fault_ok = g_forensics.observed_fault_addr == 0ull;
  bool magic_ok = g_forensics.fpsimd_magic == kFpsimdMagic;
  bool size_ok = g_forensics.fpsimd_size == 528u;
  bool post_x0_ok = post_x0 == kPostX0Sentinel;

  // Reinterpret 3.140625 as 64-bit IEEE-754 bits — what fmov d0 will
  // produce on a vreg lane[0]. v0 also has lane[1] = 0.
  uint64_t expected_v0_lo;
  std::memcpy(&expected_v0_lo, &seed, sizeof(expected_v0_lo));
  bool v0_ok = magic_ok && g_forensics.observed_v0_lo == expected_v0_lo &&
               g_forensics.observed_v0_hi == 0ull;

  bool all_ok =
      handler_ran && x0_pre_ok && x10_pre_ok && fault_ok && magic_ok && size_ok && post_x0_ok && v0_ok;

  char buf[512];
  std::snprintf(buf, sizeof(buf),
                "[SIG11-RECOVERY:%s called=%u pc=0x%lx fault=0x%lx "
                "x0_pre=0x%lx x10_pre=0x%lx magic=0x%x size=%u "
                "fpsr=0x%x fpcr=0x%x v0_lo=0x%lx v0_hi=0x%lx "
                "x0_post=0x%lx exp_v0_lo=0x%lx]",
                all_ok ? "PASS" : "FAIL",
                g_forensics.handler_called,
                static_cast<unsigned long>(g_forensics.observed_pc),
                static_cast<unsigned long>(g_forensics.observed_fault_addr),
                static_cast<unsigned long>(g_forensics.observed_x0_pre),
                static_cast<unsigned long>(g_forensics.observed_x10_pre),
                g_forensics.fpsimd_magic,
                g_forensics.fpsimd_size,
                g_forensics.observed_fpsr,
                g_forensics.observed_fpcr,
                static_cast<unsigned long>(g_forensics.observed_v0_lo),
                static_cast<unsigned long>(g_forensics.observed_v0_hi),
                static_cast<unsigned long>(post_x0),
                static_cast<unsigned long>(expected_v0_lo));
  *report = buf;
  __android_log_print(all_ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
                      kLogTag, "%s", buf);
  return all_ok;
}

// box (3): SIGUSR1 stress test.
//
// Property under test: rapid SIGUSR1 delivery from a sibling thread during
// heavy pthread_mutex contention must not deadlock or lose iterations. This
// stresses the futex-based pthread_mutex_{lock,unlock} path under EINTR-style
// interruptions (futex syscalls can return -EINTR when a signal arrives mid-
// wait; bionic's pthread_mutex retries internally).
//
// Layout:
//   - Two worker threads each run kStressIterations of lock-then-unlock on a
//     shared mutex. The critical section is intentionally tiny so the workers
//     spend most cycles in lock-contention futex sleep, maximising the chance
//     a SIGUSR1 lands during a futex wait.
//   - One signaler thread spins for the duration, repeatedly pthread_kill'ing
//     SIGUSR1 to both workers. The signaler is spawned BEFORE the workers and
//     gated on a "workers_armed" atomic so its tight loop is already running
//     by the time worker[0]/[1] begin their iteration loops — without this
//     ordering the workers can finish 100 000 lock/unlock cycles in ~10 ms,
//     beating the signaler-startup latency and observing zero signals.
//   - A wall-clock deadline bounds the test: if either worker hasn't finished
//     after kStressDeadlineSec the test is declared a deadlock.
constexpr int kStressIterations = 200000;
constexpr int kStressDeadlineSec = 10;
// Sleep between signal bursts to avoid livelocking the workers via signal
// storm. ~10 kHz (100 us between bursts) is rapid enough to land repeatedly
// during the futex waits inside pthread_mutex_lock — under a single 200k-
// iteration run the workers receive ~10^3 signals each — while still
// leaving them enough CPU time to make forward progress between
// interruptions. Without this throttle the signaler delivered >10^6
// SIGUSR1/s and the workers livelocked, never completing.
constexpr long kSignalerSleepNs = 100'000;

struct StressWorkerArg {
  pthread_mutex_t* mutex;
  std::atomic<uint64_t>* iters_done;
  std::atomic<bool>* should_stop;
  std::atomic<bool>* armed;
};

struct StressForensics {
  std::atomic<uint32_t> signals_observed;
  std::atomic<uint64_t> worker0_iters;
  std::atomic<uint64_t> worker1_iters;
};

StressForensics g_stress{};
std::atomic<bool> g_signaler_stop{false};
std::atomic<bool> g_workers_armed{false};

void Sigusr1Handler(int /*sig*/, siginfo_t* /*info*/, void* /*ucontext*/) {
  g_stress.signals_observed.fetch_add(1, std::memory_order_relaxed);
}

void* StressWorker(void* raw_arg) {
  auto* arg = static_cast<StressWorkerArg*>(raw_arg);
  // Wait until the main thread has spawned the signaler and set armed,
  // so the signaler's pthread_kill loop is already pummelling us.
  while (!arg->armed->load(std::memory_order_acquire)) {
    asm volatile("yield" ::: "memory");
  }
  for (int i = 0; i < kStressIterations; ++i) {
    pthread_mutex_lock(arg->mutex);
    asm volatile("" ::: "memory");
    pthread_mutex_unlock(arg->mutex);
    arg->iters_done->fetch_add(1, std::memory_order_relaxed);
  }
  return nullptr;
}

struct StressSignalerArg {
  std::atomic<pthread_t>* targets;  // pointer to caller-owned 2-element array
};

// Diagnostic: count successful pthread_kill calls so we can tell
// "signaler never ran" from "pthread_kill returned ESRCH" from
// "signals delivered but handler silent".
std::atomic<uint64_t> g_pthread_kill_attempts{0};
std::atomic<uint64_t> g_pthread_kill_failures{0};

void* StressSignaler(void* raw_arg) {
  auto* arg = static_cast<StressSignalerArg*>(raw_arg);
  struct timespec sleep_ts = {0, kSignalerSleepNs};
  while (!g_signaler_stop.load(std::memory_order_relaxed)) {
    pthread_t t0 = arg->targets[0].load(std::memory_order_acquire);
    pthread_t t1 = arg->targets[1].load(std::memory_order_acquire);
    if (t0) {
      g_pthread_kill_attempts.fetch_add(1, std::memory_order_relaxed);
      if (pthread_kill(t0, SIGUSR1) != 0)
        g_pthread_kill_failures.fetch_add(1, std::memory_order_relaxed);
    }
    if (t1) {
      g_pthread_kill_attempts.fetch_add(1, std::memory_order_relaxed);
      if (pthread_kill(t1, SIGUSR1) != 0)
        g_pthread_kill_failures.fetch_add(1, std::memory_order_relaxed);
    }
    nanosleep(&sleep_ts, nullptr);
  }
  return nullptr;
}

bool RunSigusr1StressTest(std::string* report) {
  g_stress.signals_observed.store(0);
  g_stress.worker0_iters.store(0);
  g_stress.worker1_iters.store(0);
  g_signaler_stop.store(false);
  g_workers_armed.store(false);
  g_pthread_kill_attempts.store(0);
  g_pthread_kill_failures.store(0);

  struct sigaction prev_sa{};
  struct sigaction new_sa{};
  new_sa.sa_sigaction = &Sigusr1Handler;
  new_sa.sa_flags = SA_SIGINFO;  // explicitly NOT SA_RESTART — let futex see EINTR
  sigemptyset(&new_sa.sa_mask);
  if (sigaction(SIGUSR1, &new_sa, &prev_sa) != 0) {
    *report = "[SIGUSR1-STRESS:FAIL sigaction errno]";
    return false;
  }
  // Explicitly unblock SIGUSR1 on this thread to rule out an inherited
  // signal mask hiding the smoking gun.
  sigset_t unblock;
  sigemptyset(&unblock);
  sigaddset(&unblock, SIGUSR1);
  pthread_sigmask(SIG_UNBLOCK, &unblock, nullptr);
  // Self-test: raise SIGUSR1 from main and verify the handler runs. If this
  // counter doesn't increment, the handler installation itself is broken and
  // there's no point continuing with the worker/signaler dance.
  uint32_t self_test_before = g_stress.signals_observed.load();
  raise(SIGUSR1);
  uint32_t self_test_after = g_stress.signals_observed.load();
  bool self_test_ok = self_test_after > self_test_before;
  // Reset so the visible signals=N number reflects only signaler-delivered
  // signals during the contention loop.
  g_stress.signals_observed.store(0);

  pthread_mutex_t mu;
  pthread_mutex_init(&mu, nullptr);

  StressWorkerArg w0_arg{&mu, &g_stress.worker0_iters, &g_signaler_stop, &g_workers_armed};
  StressWorkerArg w1_arg{&mu, &g_stress.worker1_iters, &g_signaler_stop, &g_workers_armed};

  // Spawn signaler BEFORE workers, with target slots initially null. Fill the
  // slots as worker pthread_t's become known, then arm. This guarantees the
  // signaler's busy-loop is hot well before workers begin their iteration
  // loop, so signals reliably land during contention.
  std::atomic<pthread_t> target_slots[2];
  target_slots[0].store(0);
  target_slots[1].store(0);
  StressSignalerArg sig_arg{target_slots};
  pthread_t signaler;
  if (pthread_create(&signaler, nullptr, &StressSignaler, &sig_arg) != 0) {
    *report = "[SIGUSR1-STRESS:FAIL signaler pthread_create]";
    sigaction(SIGUSR1, &prev_sa, nullptr);
    pthread_mutex_destroy(&mu);
    return false;
  }

  pthread_t w0, w1;
  if (pthread_create(&w0, nullptr, &StressWorker, &w0_arg) != 0 ||
      pthread_create(&w1, nullptr, &StressWorker, &w1_arg) != 0) {
    *report = "[SIGUSR1-STRESS:FAIL worker pthread_create]";
    g_signaler_stop.store(true);
    pthread_join(signaler, nullptr);
    sigaction(SIGUSR1, &prev_sa, nullptr);
    pthread_mutex_destroy(&mu);
    return false;
  }
  target_slots[0].store(w0, std::memory_order_release);
  target_slots[1].store(w1, std::memory_order_release);
  // Tiny grace period so the signaler observes the targets and starts
  // delivering before the workers begin spinning.
  struct timespec warmup = {0, 10 * 1000 * 1000};  // 10 ms
  nanosleep(&warmup, nullptr);
  g_workers_armed.store(true, std::memory_order_release);

  // Poll worker iteration counters until both complete all iterations or
  // the deadline elapses. We avoid pthread_timedjoin_np because it isn't
  // unconditionally exposed by bionic's NDK headers; an iters-done atomic
  // gives us the same termination signal with portable primitives. On
  // deadlock the threads are intentionally leaked (one-shot JNI call) and
  // we log FAIL so test-samples.sh can flag the regression.
  struct timespec deadline_ts;
  clock_gettime(CLOCK_MONOTONIC, &deadline_ts);
  deadline_ts.tv_sec += kStressDeadlineSec;

  bool finished = false;
  while (true) {
    if (g_stress.worker0_iters.load(std::memory_order_relaxed) ==
            static_cast<uint64_t>(kStressIterations) &&
        g_stress.worker1_iters.load(std::memory_order_relaxed) ==
            static_cast<uint64_t>(kStressIterations)) {
      finished = true;
      break;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline_ts.tv_sec ||
        (now.tv_sec == deadline_ts.tv_sec && now.tv_nsec >= deadline_ts.tv_nsec)) {
      break;
    }
    struct timespec slice = {0, 5 * 1000 * 1000};  // 5 ms
    nanosleep(&slice, nullptr);
  }

  g_signaler_stop.store(true);
  pthread_join(signaler, nullptr);

  if (finished) {
    pthread_join(w0, nullptr);
    pthread_join(w1, nullptr);
  }

  bool iters_complete = finished;
  uint32_t signals = g_stress.signals_observed.load();
  bool signals_landed = signals > 0;
  bool all_ok = self_test_ok && iters_complete && signals_landed;

  sigaction(SIGUSR1, &prev_sa, nullptr);
  if (finished) {
    pthread_mutex_destroy(&mu);
  }  // else: mutex may still be held by a stuck worker; intentionally leak

  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "[SIGUSR1-STRESS:%s self_test=%d finished=%d "
                "w0_iters=%lu/%d w1_iters=%lu/%d signals=%u "
                "kill_attempts=%lu kill_failures=%lu]",
                all_ok ? "PASS" : "FAIL", self_test_ok ? 1 : 0,
                finished ? 1 : 0,
                static_cast<unsigned long>(g_stress.worker0_iters.load()),
                kStressIterations,
                static_cast<unsigned long>(g_stress.worker1_iters.load()),
                kStressIterations, signals,
                static_cast<unsigned long>(g_pthread_kill_attempts.load()),
                static_cast<unsigned long>(g_pthread_kill_failures.load()));
  *report = buf;
  __android_log_print(all_ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
                      kLogTag, "%s", buf);
  return all_ok;
}

// Exercises the libnativehelper proxy path. The legacy jni* helper exports
// (jniCreateString here) are marked DoBadTrampoline by the auto-generated
// trampoline table, so before Digitalis covered them, calling one through the
// bridge aborted with LOG_ALWAYS_FATAL("Bad 'jniCreateString' call"). We reach
// the symbol via dlsym (it isn't in the NDK sysroot to link against) so the
// call routes through the proxy trampoline; a valid, correct-length jstring
// proves the Digitalis-side custom trampoline (JNIEnv translation via
// ToHostJNIEnv) is installed and forwarding to host libnativehelper.
bool RunLibnativehelperProxyTest(JNIEnv* env, std::string* report) {
  void* handle = dlopen("libnativehelper.so", RTLD_NOW);
  if (handle == nullptr) {
    *report = "[LIBNH-PROXY:FAIL dlopen]";
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", report->c_str());
    return false;
  }
  using CreateStringFn = jstring (*)(JNIEnv*, const jchar*, jsize);
  auto create_string =
      reinterpret_cast<CreateStringFn>(dlsym(handle, "jniCreateString"));
  if (create_string == nullptr) {
    *report = "[LIBNH-PROXY:FAIL dlsym jniCreateString]";
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", report->c_str());
    return false;
  }

  // UTF-16 "hello"; jchar is uint16_t.
  static const jchar kHello[] = {0x0068, 0x0065, 0x006c, 0x006c, 0x006f};
  // If jniCreateString were still DoBadTrampoline this call aborts the process
  // (SIGABRT), which test-samples.sh flags as a crash.
  jstring created = create_string(env, kHello, 5);
  bool non_null = created != nullptr;
  jsize len = non_null ? env->GetStringLength(created) : -1;
  bool len_ok = len == 5;

  // Also exercise jniRegisterNativeMethods (FindClass + RegisterNatives). With
  // numMethods=0 it registers nothing and returns JNI_OK, so it's a side-effect-
  // free way to drive the full trampoline path. A surviving DoBadTrampoline
  // would abort here; a non-OK return would mean FindClass failed.
  using RegisterFn = jint (*)(JNIEnv*, const char*, const JNINativeMethod*, jint);
  auto register_natives =
      reinterpret_cast<RegisterFn>(dlsym(handle, "jniRegisterNativeMethods"));
  bool reg_ok = false;
  if (register_natives != nullptr) {
    jint rc = register_natives(env, "com/example/hellojni/HelloJni", nullptr, 0);
    reg_ok = rc == JNI_OK;
  }

  bool all_ok = non_null && len_ok && reg_ok;

  char buf[200];
  std::snprintf(buf, sizeof(buf), "[LIBNH-PROXY:%s non_null=%d len=%d reg_natives=%d]",
                all_ok ? "PASS" : "FAIL", non_null ? 1 : 0, len, reg_ok ? 1 : 0);
  *report = buf;
  __android_log_print(all_ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kLogTag,
                      "%s", buf);
  return all_ok;
}

}  // namespace

jstring StringFromJni(JNIEnv* env, jobject) {
  std::string report;
  RunSigsegvRecoveryTest(&report);
  std::string stress_report;
  RunSigusr1StressTest(&stress_report);
  std::string libnh_report;
  RunLibnativehelperProxyTest(env, &libnh_report);
  // Keep the user-visible text identical to the legacy sample so the
  // existing screenshot reference still matches; the recovery report is
  // visible in logcat under tag "hello-jni".
  std::string hello = "Hello from JNI.";
  return env->NewStringUTF(hello.c_str());
}

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM* _Nonnull vm, void* _Nullable) {
  JNIEnv* env;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  jclass c = env->FindClass("com/example/hellojni/HelloJni");
  if (c == nullptr) return JNI_ERR;

  static const JNINativeMethod methods[] = {
      {"stringFromJNI", "()Ljava/lang/String;",
       reinterpret_cast<void*>(StringFromJni)},
  };
  int rc = env->RegisterNatives(c, methods, sizeof(methods) / sizeof(methods[0]));
  if (rc != JNI_OK) return rc;

  return JNI_VERSION_1_6;
}
