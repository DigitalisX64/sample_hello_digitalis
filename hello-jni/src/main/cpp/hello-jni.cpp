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
#include <fenv.h>
#include <signal.h>
#include <ucontext.h>

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

}  // namespace

jstring StringFromJni(JNIEnv* env, jobject) {
  std::string report;
  RunSigsegvRecoveryTest(&report);
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
