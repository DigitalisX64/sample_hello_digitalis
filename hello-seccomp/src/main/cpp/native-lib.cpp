// Integration-level probe for guest seccomp-bpf filter installation, modelling
// what a sandboxed engine (Chromium / Gecko renderer, an isolated-process
// worker) does at startup.
//
// The interesting path for the translator is seccomp(SECCOMP_SET_MODE_FILTER):
// a guest builds its seccomp-bpf program for the GUEST ABI -- the architecture
// gate compares seccomp_data.arch against AUDIT_ARCH_AARCH64 and the per-syscall
// checks use AArch64 syscall numbers. Under binary translation the process
// actually runs as x86_64 and Berberis issues host x86_64 syscalls, so a filter
// forwarded verbatim never matches the running architecture: the first
// post-install syscall trips the wrong-architecture branch and the kernel raises
// SIGSYS (signal 31), killing the process. That is exactly what kills a Chromium
// renderer at launch. The translator must therefore NOT install a guest seccomp
// filter -- it reports success to the guest while leaving the process unfiltered
// (the host zygote's native seccomp policy still confines the app).
//
// This probe installs a faithful Chromium-style arch-gate filter that KILLS the
// process on any architecture other than AArch64, then deliberately makes
// syscalls. On a correct translator the filter is neutered (never installed),
// so the syscalls succeed and the probe survives to report PASS. On a regression
// that forwards the guest filter to the host kernel, the x86_64 process takes the
// wrong-arch branch and is killed by the kernel -- the StatusTest harness then
// observes the "Fatal signal 31" crash. On real AArch64 hardware the gate matches
// and allows everything, so the probe is well-behaved there too.

#include <android/log.h>
#include <jni.h>

#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <string>

#define LOG_TAG "helloseccomp"

namespace {

// A Chromium-style architecture gate: allow AArch64 (the guest ABI), kill on any
// other architecture. Installed on an x86_64 host process (the regression case),
// the very next syscall takes the kill branch.
long InstallArchGateFilter() {
  struct sock_filter insns[] = {
      // Load seccomp_data.arch.
      BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
               static_cast<__u32>(offsetof(struct seccomp_data, arch))),
      // if arch == AUDIT_ARCH_AARCH64 -> skip the kill (jump +1).
      BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
      // wrong architecture -> kill the whole process.
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
      // right architecture -> allow.
      BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
  };
  struct sock_fprog prog = {
      .len = static_cast<unsigned short>(sizeof(insns) / sizeof(insns[0])),
      .filter = insns,
  };
  // Go through the seccomp() syscall directly (what a modern guest engine uses),
  // not the legacy prctl(PR_SET_SECCOMP) path.
  return syscall(__NR_seccomp, SECCOMP_SET_MODE_FILTER, 0u, &prog);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloseccomp_MainActivity_probeSeccomp(JNIEnv* env,
                                                        jobject /*this*/) {
  std::string report = "guest seccomp-bpf filter probe:\n";
  char buf[256];

  // A non-privileged seccomp filter requires NO_NEW_PRIVS (or CAP_SYS_ADMIN).
  // Setting it means that, absent the translator's neutering, the host kernel
  // would genuinely install the filter -- so surviving the install is a real
  // signal, not an artifact of the call being rejected for lack of privilege.
  const int nnp = prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
  snprintf(buf, sizeof(buf), "  PR_SET_NO_NEW_PRIVS -> %d\n", nnp);
  report += buf;

  // Install the arch-gate filter. On a correct translator this is a no-op that
  // reports success; on a regression the process is killed by SIGSYS at the next
  // syscall and never reaches the lines below.
  const long rc = InstallArchGateFilter();
  snprintf(buf, sizeof(buf), "  seccomp(SET_MODE_FILTER) -> %ld\n", rc);
  report += buf;

  // Force real syscalls through the (would-be) filter. On a regression the
  // wrong-arch branch kills us here; on a correct translator these succeed.
  const long pid = syscall(__NR_getpid);
  const int mode = prctl(PR_GET_SECCOMP);
  snprintf(buf, sizeof(buf),
           "  post-install getpid=%ld PR_GET_SECCOMP=%d\n",
           pid, mode);
  report += buf;

  // Reaching here means the process survived installing a kill-on-wrong-arch
  // filter: the translator correctly neutered the guest seccomp filter.
  report += "  survived guest filter install -> PASS\n";

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
