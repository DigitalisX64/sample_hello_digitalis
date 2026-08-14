/*
 * Copyright (C) 2026 utzcoz
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
 */

#include <android/log.h>
#include <android/sharedmem.h>
#include <android/sharedmem_jni.h>
#include <jni.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>

#define LOG_TAG "hellosharedmem"

namespace {

constexpr size_t kSize = 128 * 1024;
constexpr size_t kBlock = 4096;

// Deterministic patterns, pure functions of the absolute byte offset.
uint8_t PatternA(size_t i) { return static_cast<uint8_t>((i * 131u + 7u) ^ (i >> 8)); }
uint8_t PatternB(size_t i) { return static_cast<uint8_t>(i * 37u + 11u); }  // written via dup'd fd
uint8_t PatternC(size_t i) { return static_cast<uint8_t>(i * 73u + 29u); }  // peer-thread reply
// PatternD is mirrored in MainActivity.kt for the JNI interop check.
uint8_t PatternD(size_t i) { return static_cast<uint8_t>(i * 31u + 5u); }

struct Probe {
  std::string errors;  // accumulated "FAIL at ..." entries
  std::string notes;   // informational (never contains the FAIL marker)
  int failures = 0;

  void Fail(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "hellosharedmem FAIL at %s", buf);
    if (!errors.empty()) errors += "; ";
    errors += "FAIL at ";
    errors += buf;
    ++failures;
  }

  void Note(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
    if (!notes.empty()) notes += "; ";
    notes += buf;
  }

  bool CheckRegion(const uint8_t* p, size_t off, size_t len, uint8_t (*pat)(size_t),
                   const char* tag) {
    for (size_t i = off; i < off + len; ++i) {
      if (p[i] != pat(i)) {
        Fail("%s: [%zu]=0x%02x want 0x%02x", tag, i, p[i], pat(i));
        return false;
      }
    }
    return true;
  }
};

void FillRegion(uint8_t* p, size_t off, size_t len, uint8_t (*pat)(size_t)) {
  for (size_t i = off; i < off + len; ++i) p[i] = pat(i);
}

// ---- SCM_RIGHTS peer thread: receives the fd like a real IPC consumer would.

struct PeerCtx {
  int sock = -1;
  size_t size = 0;
  char err[224] = {0};  // empty string == success
};

void* PeerThreadMain(void* arg) {
  PeerCtx* ctx = static_cast<PeerCtx*>(arg);
  pollfd pfd = {ctx->sock, POLLIN, 0};
  if (poll(&pfd, 1, 2000) != 1) {
    snprintf(ctx->err, sizeof(ctx->err), "peer-recv: timeout waiting for fd");
    return nullptr;
  }
  char byte = 0;
  iovec iov = {&byte, 1};
  alignas(cmsghdr) char cbuf[CMSG_SPACE(sizeof(int))] = {0};
  msghdr mh = {};
  mh.msg_iov = &iov;
  mh.msg_iovlen = 1;
  mh.msg_control = cbuf;
  mh.msg_controllen = sizeof(cbuf);
  if (recvmsg(ctx->sock, &mh, 0) != 1) {
    snprintf(ctx->err, sizeof(ctx->err), "peer-recv: recvmsg errno=%d (%s)", errno,
             strerror(errno));
    return nullptr;
  }
  cmsghdr* cm = CMSG_FIRSTHDR(&mh);
  if (cm == nullptr || cm->cmsg_level != SOL_SOCKET || cm->cmsg_type != SCM_RIGHTS) {
    snprintf(ctx->err, sizeof(ctx->err), "peer-recv: no SCM_RIGHTS control message");
    return nullptr;
  }
  int fd = -1;
  memcpy(&fd, CMSG_DATA(cm), sizeof(fd));

  size_t sz = ASharedMemory_getSize(fd);
  if (sz != ctx->size) {
    snprintf(ctx->err, sizeof(ctx->err), "peer-size: getSize=%zu want %zu on received fd", sz,
             ctx->size);
    close(fd);
    return nullptr;
  }
  void* raw = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (raw == MAP_FAILED) {
    snprintf(ctx->err, sizeof(ctx->err), "peer-mmap: errno=%d (%s)", errno, strerror(errno));
    close(fd);
    return nullptr;
  }
  uint8_t* p = static_cast<uint8_t*>(raw);
  bool ok = true;
  for (size_t i = 0; i < kBlock && ok; ++i) {
    if (p[i] != PatternB(i)) {
      snprintf(ctx->err, sizeof(ctx->err), "peer-verify: [%zu]=0x%02x want 0x%02x (B)", i, p[i],
               PatternB(i));
      ok = false;
    }
  }
  for (size_t i = kBlock; i < sz && ok; ++i) {
    if (p[i] != PatternA(i)) {
      snprintf(ctx->err, sizeof(ctx->err), "peer-verify: [%zu]=0x%02x want 0x%02x (A)", i, p[i],
               PatternA(i));
      ok = false;
    }
  }
  if (ok) FillRegion(p, kBlock, kBlock, PatternC);  // reply pattern for the main thread
  munmap(p, sz);
  close(fd);
  char reply = ok ? 'K' : 'E';
  send(ctx->sock, &reply, 1, 0);
  return nullptr;
}

std::string RunProbe() {
  Probe probe;

  // 1. create + getSize (128 KiB is a page multiple, so exact match is required;
  //    the header documents no rounding).
  int fd = ASharedMemory_create("digitalis-probe", kSize);
  if (fd < 0) {
    probe.Fail("create: fd=%d errno=%d (%s)", fd, errno, strerror(errno));
    return "hellosharedmem " + probe.errors;
  }
  size_t size = ASharedMemory_getSize(fd);
  if (size != kSize) probe.Fail("getSize: %zu want %zu", size, kSize);

  // 2. Write a deterministic pattern, verify, unmap, re-map: the content must
  //    persist (a real shared region, not anonymous-per-mapping).
  void* raw = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (raw == MAP_FAILED) {
    probe.Fail("mmap-rw: errno=%d (%s)", errno, strerror(errno));
    close(fd);
    return "hellosharedmem " + probe.errors;
  }
  uint8_t* map1 = static_cast<uint8_t*>(raw);
  FillRegion(map1, 0, kSize, PatternA);
  probe.CheckRegion(map1, 0, kSize, PatternA, "readback");
  munmap(map1, kSize);
  raw = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (raw == MAP_FAILED) {
    probe.Fail("re-mmap: errno=%d (%s)", errno, strerror(errno));
    close(fd);
    return "hellosharedmem " + probe.errors;
  }
  map1 = static_cast<uint8_t*>(raw);
  probe.CheckRegion(map1, 0, kSize, PatternA, "persistence");

  // 3. dup() the fd; both mappings must observe the same bytes simultaneously.
  uint8_t* map2 = nullptr;
  int fd2 = dup(fd);
  if (fd2 < 0) {
    probe.Fail("dup: errno=%d (%s)", errno, strerror(errno));
  } else {
    if (ASharedMemory_getSize(fd2) != kSize)
      probe.Fail("dup-getSize: %zu want %zu", ASharedMemory_getSize(fd2), kSize);
    raw = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (raw == MAP_FAILED) {
      probe.Fail("dup-mmap: errno=%d (%s)", errno, strerror(errno));
    } else {
      map2 = static_cast<uint8_t*>(raw);
      probe.CheckRegion(map2, 0, kSize, PatternA, "dup-view");
      FillRegion(map2, 0, kBlock, PatternB);                          // write through the dup
      probe.CheckRegion(map1, 0, kBlock, PatternB, "dup-coherence");  // read through the original
    }
  }

  // 4. Pass the fd to a second thread over a socketpair with SCM_RIGHTS; the
  //    thread maps it, verifies, and writes a reply pattern. 2 s timeouts on
  //    both sides of the handoff.
  if (map2 != nullptr) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
      probe.Fail("socketpair: errno=%d (%s)", errno, strerror(errno));
    } else {
      PeerCtx ctx;
      ctx.sock = sv[1];
      ctx.size = kSize;
      pthread_t th;
      if (pthread_create(&th, nullptr, PeerThreadMain, &ctx) != 0) {
        probe.Fail("pthread_create: errno=%d (%s)", errno, strerror(errno));
      } else {
        char byte = 'F';
        iovec iov = {&byte, 1};
        alignas(cmsghdr) char cbuf[CMSG_SPACE(sizeof(int))] = {0};
        msghdr mh = {};
        mh.msg_iov = &iov;
        mh.msg_iovlen = 1;
        mh.msg_control = cbuf;
        mh.msg_controllen = sizeof(cbuf);
        cmsghdr* cm = CMSG_FIRSTHDR(&mh);
        cm->cmsg_level = SOL_SOCKET;
        cm->cmsg_type = SCM_RIGHTS;
        cm->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cm), &fd, sizeof(fd));
        if (sendmsg(sv[0], &mh, 0) != 1)
          probe.Fail("scm-send: errno=%d (%s)", errno, strerror(errno));
        pollfd pfd = {sv[0], POLLIN, 0};
        if (poll(&pfd, 1, 2000) != 1) {
          probe.Fail("scm-reply: timeout waiting for peer thread");
        } else {
          char reply = 0;
          recv(sv[0], &reply, 1, 0);  // 'E' detail surfaces via ctx.err below
        }
        pthread_join(th, nullptr);  // bounded: the thread's own waits are 2 s
        if (ctx.err[0] != 0) {
          probe.Fail("%s", ctx.err);
        } else {
          probe.CheckRegion(map1, kBlock, kBlock, PatternC, "peer-reply");
          probe.CheckRegion(map1, 2 * kBlock, kSize - 2 * kBlock, PatternA, "peer-untouched");
        }
      }
      close(sv[0]);
      close(sv[1]);
    }
  }

  // 5. Odd-size region: the header documents no rounding, so only >= requested
  //    is asserted; whether getSize is exact or page-rounded is logged.
  int ofd = ASharedMemory_create("digitalis-odd", 1000);
  if (ofd < 0) {
    probe.Fail("create-odd: fd=%d errno=%d (%s)", ofd, errno, strerror(errno));
  } else {
    size_t osz = ASharedMemory_getSize(ofd);
    if (osz < 1000) {
      probe.Fail("getSize-odd: %zu < requested 1000", osz);
    } else {
      probe.Note(osz == 1000 ? "odd-size getSize exact (1000)" : "odd-size getSize rounded to %zu",
                 osz);
      raw = mmap(nullptr, 1000, PROT_READ | PROT_WRITE, MAP_SHARED, ofd, 0);
      if (raw == MAP_FAILED) {
        probe.Fail("odd-mmap: errno=%d (%s)", errno, strerror(errno));
      } else {
        uint8_t* op = static_cast<uint8_t*>(raw);
        FillRegion(op, 0, 1000, PatternA);
        probe.CheckRegion(op, 0, 1000, PatternA, "odd-readback");
        munmap(op, 1000);
      }
    }
    close(ofd);
  }

  // 6. setProt(PROT_READ) seals the region for every fd system-wide; new
  //    writable mappings must be rejected, existing mappings are unaffected,
  //    and access can only be removed, never added back (per the header).
  if (ASharedMemory_setProt(fd, PROT_READ) != 0) {
    probe.Fail("setProt-ro: errno=%d (%s)", errno, strerror(errno));
  } else {
    raw = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (raw != MAP_FAILED) {
      probe.Fail("sealed-mmap-rw: writable mapping succeeded after setProt(PROT_READ)");
      munmap(raw, kSize);
    } else {
      // Header does not pin the errno; EPERM (memfd seal) or EACCES (ashmem)
      // expected in practice — record which without asserting.
      probe.Note("sealed rw mmap rejected, errno=%d (%s)", errno, strerror(errno));
    }
    if (fd2 >= 0) {
      raw = mmap(nullptr, kSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
      if (raw != MAP_FAILED) {
        probe.Fail("sealed-dup-mmap-rw: seal did not apply to the dup'd fd");
        munmap(raw, kSize);
      }
    }
    raw = mmap(nullptr, kSize, PROT_READ, MAP_SHARED, fd, 0);
    if (raw == MAP_FAILED) {
      probe.Fail("sealed-mmap-ro: errno=%d (%s)", errno, strerror(errno));
    } else {
      const uint8_t* rp = static_cast<const uint8_t*>(raw);
      probe.CheckRegion(rp, 0, kBlock, PatternB, "sealed-ro-view-b");
      probe.CheckRegion(rp, kBlock, kBlock, PatternC, "sealed-ro-view-c");
      probe.CheckRegion(rp, 2 * kBlock, kSize - 2 * kBlock, PatternA, "sealed-ro-view-a");
      munmap(const_cast<uint8_t*>(rp), kSize);
    }
    probe.CheckRegion(map1, 2 * kBlock, kBlock, PatternA, "sealed-existing-read");
    if (map2 != nullptr) {
      FillRegion(map1, 2 * kBlock, kBlock, PatternB);  // pre-seal mapping stays writable
      probe.CheckRegion(map2, 2 * kBlock, kBlock, PatternB, "sealed-existing-write");
    }
    if (ASharedMemory_setProt(fd, PROT_READ | PROT_WRITE) == 0)
      probe.Fail("setProt-upgrade: adding PROT_WRITE back succeeded; header says access can only "
                 "be removed");
    else
      probe.Note("setProt upgrade rejected, errno=%d (%s)", errno, strerror(errno));
  }

  // 7. Edge probes. Size 0: the header documents the return value -EINVAL
  //    (not -1 + errno). Name is optional. getSize on invalid fds returns 0.
  int zfd = ASharedMemory_create("digitalis-zero", 0);
  if (zfd != -EINVAL) {
    probe.Fail("create-zero: got %d want -EINVAL(%d) per header", zfd, -EINVAL);
    if (zfd >= 0) close(zfd);
  }
  int nfd = ASharedMemory_create(nullptr, kBlock);
  if (nfd < 0)
    probe.Fail("create-noname: fd=%d errno=%d (%s)", nfd, errno, strerror(errno));
  else
    close(nfd);
  if (ASharedMemory_getSize(-1) != 0) probe.Fail("getSize-badfd: nonzero for fd -1");
  int pfds[2];
  if (pipe(pfds) == 0) {
    if (ASharedMemory_getSize(pfds[0]) != 0) probe.Fail("getSize-pipe: nonzero for a pipe fd");
    close(pfds[0]);
    close(pfds[1]);
  }

  if (map2 != nullptr) munmap(map2, kSize);
  munmap(map1, kSize);
  if (fd2 >= 0) close(fd2);
  close(fd);

  if (probe.failures != 0) return "hellosharedmem " + probe.errors;
  std::string msg =
      "hellosharedmem OK: create/getSize, persistence, dup coherence, SCM_RIGHTS thread "
      "handoff, seal-to-read-only, edge probes all verified (" +
      probe.notes + ")";
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
  return msg;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellosharedmem_MainActivity_nativeProbe(JNIEnv* env, jobject /*this*/) {
  return env->NewStringUTF(RunProbe().c_str());
}

// Kotlin created an android.os.SharedMemory; dup its fd, map it, and fill
// PatternD for the Kotlin side to verify through SharedMemory.mapReadOnly().
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellosharedmem_MainActivity_nativeFillFromJava(JNIEnv* env, jobject /*this*/,
                                                                jobject shared_memory,
                                                                jint size) {
  char buf[192];
  int fd = ASharedMemory_dupFromJava(env, shared_memory);
  if (fd < 0) {
    snprintf(buf, sizeof(buf), "FAIL at jni-dupFromJava: fd=%d errno=%d (%s)", fd, errno,
             strerror(errno));
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "hellosharedmem %s", buf);
    return env->NewStringUTF(buf);
  }
  size_t sz = ASharedMemory_getSize(fd);
  if (sz != static_cast<size_t>(size)) {
    snprintf(buf, sizeof(buf), "FAIL at jni-getSize: %zu want %d", sz, size);
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "hellosharedmem %s", buf);
    close(fd);
    return env->NewStringUTF(buf);
  }
  void* raw = mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (raw == MAP_FAILED) {
    snprintf(buf, sizeof(buf), "FAIL at jni-mmap: errno=%d (%s)", errno, strerror(errno));
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "hellosharedmem %s", buf);
    close(fd);
    return env->NewStringUTF(buf);
  }
  uint8_t* p = static_cast<uint8_t*>(raw);
  FillRegion(p, 0, sz, PatternD);
  bool ok = true;
  for (size_t i = 0; i < sz && ok; ++i) {
    if (p[i] != PatternD(i)) {
      snprintf(buf, sizeof(buf), "FAIL at jni-readback: [%zu]=0x%02x want 0x%02x", i, p[i],
               PatternD(i));
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "hellosharedmem %s", buf);
      ok = false;
    }
  }
  munmap(p, sz);
  close(fd);
  if (!ok) return env->NewStringUTF(buf);
  snprintf(buf, sizeof(buf), "OK: dup'd fd mapped, %zu bytes written natively", sz);
  return env->NewStringUTF(buf);
}
