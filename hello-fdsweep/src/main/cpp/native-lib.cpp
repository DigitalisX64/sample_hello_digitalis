// Integration probe for process-spawn fd hygiene under translation.
//
// A subprocess launcher (Chromium's LaunchProcess/CloseSuperfluousFds, bionic
// posix_spawn) forks a child, and the child closes every fd it doesn't
// recognize, resets signal dispositions, then execs. Natively that sweep only
// touches the app's own descriptors, but under translation the translator's
// internal fds (the translation-cache memfds) live in the SAME fd table. If
// the translator doesn't protect them (fdsan host-owned tags honoured by the
// guest close/close_range emulation), the sweep closes the cached memfd and
// the child's next translation-cache child-table allocation aborts inside
// MmapImplOrDie with EBADF (observed in the field: Chromium fork children
// dying with the mmap CHECK while resetting sigaction handlers).
//
// Probe parts:
//  1. fork(); the child sweeps fds 3..511 via per-fd close() and the rest of
//     the table via the close_range syscall (both guest emulation paths),
//     resets signal dispositions like posix_spawn's SETSIGDEF, then executes
//     freshly written code in a far 16MB address slice — which forces a new
//     translation-cache child-table allocation AFTER the sweep.
//  2. posix_spawn of /system/bin/ls (vfork + SETSIGDEF + fd actions + exec of
//     a host binary) — the real-world launcher shape end to end.

#include <android/log.h>
#include <fcntl.h>
#include <jni.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>

#define LOG_TAG "hellofdsweep"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern char** environ;

namespace {

// Child exit codes for part 1 (any signal death is the translator bug).
enum : int {
  kChildOk = 0,
  kChildMmapFailed = 3,
  kChildWrongResult = 4,
  kChildMprotectFailed = 5,
};

// Runs in the forked child: pre-exec style fd hygiene, then guest work that
// needs the translator's internal fds to still be alive.
void ChildSweepAndTranslate() {
  // Per-fd close() sweep for the low range...
  for (int fd = 3; fd < 512; fd++) {
    close(fd);
  }
  // ...and the close_range syscall for the rest of the table, so both guest
  // emulation paths are exercised. (Raw syscall: the libc wrapper needs a
  // newer target API level.)
  syscall(__NR_close_range, 512u, ~0u, 0);

  // posix_spawn-style SETSIGDEF: reset some dispositions. Returning the old
  // host-registered action to the guest makes the translator wrap it, which
  // allocates translation-cache entries post-sweep.
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_DFL;
  sigaction(SIGUSR2, &sa, nullptr);
  sigaction(SIGPIPE, &sa, nullptr);

  // Execute code in a fresh, far 16MB address slice: guarantees a new
  // translation-cache child-table allocation after the sweep.
  void* p = mmap(reinterpret_cast<void*>(0x5a00000000ull), 4096,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == MAP_FAILED) {
    _exit(kChildMmapFailed);
  }
  static const uint32_t code[2] = {0x52800540u /* mov w0, #42 */,
                                   0xd65f03c0u /* ret */};
  memcpy(p, code, sizeof(code));
  if (mprotect(p, 4096, PROT_READ | PROT_EXEC) != 0) {
    _exit(kChildMprotectFailed);
  }
  __builtin___clear_cache(static_cast<char*>(p),
                          static_cast<char*>(p) + sizeof(code));
  int (*fn)(void) = reinterpret_cast<int (*)(void)>(p);
  _exit(fn() == 42 ? kChildOk : kChildWrongResult);
}

std::string ProbeForkSweep() {
  // A few sacrificial descriptors for the sweep.
  for (int i = 0; i < 4; i++) {
    open("/dev/null", O_RDONLY);
  }

  pid_t pid = fork();
  if (pid < 0) {
    return "fork-sweep: FAIL (fork: " + std::string(strerror(errno)) + ")";
  }
  if (pid == 0) {
    ChildSweepAndTranslate();  // never returns
  }
  int st = 0;
  if (waitpid(pid, &st, 0) != pid) {
    return "fork-sweep: FAIL (waitpid)";
  }
  if (WIFSIGNALED(st)) {
    // A signal 6 death here is the translator losing its own fds to the sweep.
    return "fork-sweep: FAIL (child killed by signal " +
           std::to_string(WTERMSIG(st)) + ")";
  }
  if (!WIFEXITED(st) || WEXITSTATUS(st) != kChildOk) {
    return "fork-sweep: FAIL (child exit " + std::to_string(WEXITSTATUS(st)) +
           ")";
  }
  return "fork-sweep: OK";
}

std::string ProbePosixSpawn() {
  posix_spawn_file_actions_t fa;
  posix_spawn_file_actions_init(&fa);
  posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);

  posix_spawnattr_t attr;
  posix_spawnattr_init(&attr);
  sigset_t def;
  sigemptyset(&def);
  sigaddset(&def, SIGUSR1);
  sigaddset(&def, SIGUSR2);
  posix_spawnattr_setsigdefault(&attr, &def);
  posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

  char* const argv[] = {const_cast<char*>("ls"),
                        const_cast<char*>("/system/bin/ls"), nullptr};
  pid_t pid = 0;
  int rc = posix_spawn(&pid, "/system/bin/ls", &fa, &attr, argv, environ);
  posix_spawn_file_actions_destroy(&fa);
  posix_spawnattr_destroy(&attr);
  if (rc != 0) {
    return "posix-spawn: FAIL (rc=" + std::to_string(rc) + " " + strerror(rc) +
           ")";
  }
  int st = 0;
  if (waitpid(pid, &st, 0) != pid) {
    return "posix-spawn: FAIL (waitpid)";
  }
  if (WIFSIGNALED(st)) {
    return "posix-spawn: FAIL (child killed by signal " +
           std::to_string(WTERMSIG(st)) + ")";
  }
  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
    return "posix-spawn: FAIL (child exit " + std::to_string(WEXITSTATUS(st)) +
           ")";
  }
  return "posix-spawn: OK";
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellofdsweep_MainActivity_probeFdSweep(JNIEnv* env, jobject) {
  std::string fork_sweep = ProbeForkSweep();
  std::string spawn = ProbePosixSpawn();
  std::string msg = fork_sweep + "\n" + spawn;
  if (msg.find("FAIL") != std::string::npos) {
    LOGE("%s", msg.c_str());
  } else {
    LOGI("%s", msg.c_str());
  }
  return env->NewStringUTF(msg.c_str());
}
