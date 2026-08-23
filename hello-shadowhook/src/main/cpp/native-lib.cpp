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

// Exercises ByteDance ShadowHook's *inline hooking* engine end to end under
// Berberis ARM64->x86_64 translation. This is a high-value translator test
// because ShadowHook does not merely call APIs: it rewrites ARM64 machine code
// at runtime. To install a hook it disassembles the first few instructions of
// the target function, relocates them into a freshly-allocated trampoline
// (fixing up PC-relative operands), overwrites the target prologue with a
// branch to the proxy, and flushes the instruction cache. Every one of those
// steps runs as translated ARM64 code operating on the addresses of *other*
// translated ARM64 code, so a successful hook proves the translator keeps guest
// self-modification, IC-invalidation and PC-relative fixups coherent.
//
// The probe hooks a local function hd_target(int): after the hook is installed,
// a call to hd_target must divert through hd_proxy (setting a flag) and then
// chain back to the original by calling the saved orig pointer directly (the
// UNIQUE-mode contract). We assert both that the proxy fired and that the value
// returned through the relocated original prologue is still correct, then unhook
// and confirm the original is restored.

#include <android/log.h>
#include <jni.h>

#include <string>

#include "shadowhook.h"

#define LOG_TAG "HelloShadowhook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

// A volatile addend keeps the compiler from constant-folding hd_target(41)
// across the noinline boundary, so the call really dispatches through the
// function's prologue — the bytes ShadowHook patches.
volatile int g_target_addend = 1;

// The hook target. noinline guarantees a real out-of-line prologue exists to be
// overwritten; without it the call could be inlined and there would be nothing
// to hook.
extern "C" __attribute__((noinline)) int hd_target(int x) {
  return x + g_target_addend;
}

int (*g_orig)(int) = nullptr;
bool g_fired = false;

// The proxy installed over hd_target. When the hook is live, a call to
// hd_target lands here instead; we record that it fired, then chain to the
// original by calling the saved orig pointer (g_orig) directly, as required for
// a UNIQUE-mode hook.
extern "C" int hd_proxy(int x) {
  g_fired = true;
  // UNIQUE mode: chain to the saved original directly. (SHADOWHOOK_CALL_PREV /
  // SHADOWHOOK_POP_STACK are the MULTI/SHARED-mode hub mechanism and would
  // dereference an uninitialized per-thread hub stack here.)
  return g_orig(x);
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloshadowhook_MainActivity_runHookProbe(JNIEnv *env, jobject /*thiz*/) {
  // 1. Baseline: call the target before hooking. Establishes the un-hooked
  //    result and ensures the proxy has not fired.
  g_fired = false;
  int baseline = hd_target(41);
  if (baseline != 42) {
    std::string m = "SHADOWHOOK FAIL: baseline hd_target(41)=" + std::to_string(baseline) +
                    " want=42";
    LOGI("%s", m.c_str());
    return env->NewStringUTF(m.c_str());
  }
  if (g_fired) {
    std::string m = "SHADOWHOOK FAIL: proxy fired before hook installed";
    LOGI("%s", m.c_str());
    return env->NewStringUTF(m.c_str());
  }

  // 2. Install a real inline hook on the local function. In UNIQUE mode
  //    shadowhook_hook_sym_addr rewrites hd_target's prologue to jump to
  //    hd_proxy and stashes the relocated original through g_orig. A null stub
  //    means the rewrite failed; surface the ShadowHook errno + message.
  void *stub = shadowhook_hook_sym_addr(reinterpret_cast<void *>(hd_target),
                                        reinterpret_cast<void *>(hd_proxy),
                                        reinterpret_cast<void **>(&g_orig));
  if (stub == nullptr) {
    int err = shadowhook_get_errno();
    const char *emsg = shadowhook_to_errmsg(err);
    std::string m = "SHADOWHOOK FAIL: hook failed errno=" + std::to_string(err) + " (" +
                    (emsg ? emsg : "?") + ")";
    LOGI("%s", m.c_str());
    return env->NewStringUTF(m.c_str());
  }

  // 3. Call the target again. The hook must divert it through hd_proxy
  //    (g_fired=true) and the chained-through original must still return 42 —
  //    proving the relocated prologue executes correctly under translation.
  g_fired = false;
  int hooked = hd_target(41);
  bool fired = g_fired;

  // 4. Remove the hook and restore the original prologue.
  int unhook_rc = shadowhook_unhook(stub);

  // 5. Post-unhook: the target must be restored and must no longer divert.
  g_fired = false;
  int restored = hd_target(41);
  bool fired_after_unhook = g_fired;

  std::string m;
  if (!fired) {
    m = "SHADOWHOOK FAIL: hook did not fire (hooked call returned " +
        std::to_string(hooked) + ")";
  } else if (hooked != 42) {
    m = "SHADOWHOOK FAIL: chained original wrong: hooked hd_target(41)=" +
        std::to_string(hooked) + " want=42";
  } else if (unhook_rc != 0) {
    m = "SHADOWHOOK FAIL: unhook rc=" + std::to_string(unhook_rc) + " (" +
        (shadowhook_to_errmsg(unhook_rc) ? shadowhook_to_errmsg(unhook_rc) : "?") + ")";
  } else if (fired_after_unhook || restored != 42) {
    m = "SHADOWHOOK FAIL: not restored after unhook (fired=" +
        std::string(fired_after_unhook ? "true" : "false") + " restored=" +
        std::to_string(restored) + ")";
  } else {
    m = "SHADOWHOOK OK (hook fired, chained original=42, unhooked and restored)";
  }
  LOGI("%s", m.c_str());
  return env->NewStringUTF(m.c_str());
}
