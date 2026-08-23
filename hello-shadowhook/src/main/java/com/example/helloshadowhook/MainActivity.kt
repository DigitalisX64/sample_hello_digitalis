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
package com.example.helloshadowhook

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.bytedance.shadowhook.ShadowHook
import com.example.hellodigitalis.helloshadowhook.R

/**
 * Exercises ByteDance ShadowHook — a native inline / PLT hooking engine — under
 * Berberis ARM64->x86_64 translation.
 * Upstream: https://github.com/bytedance/android-inline-hook
 *
 * KNOWN LIMITATION (why this module is NOT in the test-samples.sh gate):
 * ShadowHook.init() fails with SHADOWHOOK_ERRNO_INIT_LINKER (12) under Berberis.
 * The root cause is understood: init resolves the dynamic linker's internal
 * symbols (soinfo::call_constructors, to monitor dlopen) via ByteDance's xDL,
 * which re-opens the linker by the path it reports through dl_iterate_phdr. The
 * guest ARM64 linker advertises "/system/bin/linker64" — which on the x86_64 host
 * image symlinks to the HOST x86_64 linker — so xDL reads a wrong-architecture
 * ELF (section headers at the guest linker's e_shoff → garbage), fails to find
 * .symtab, and aborts. (The earlier "Berberis maps the linker r-- so there is no
 * executable segment to introspect" theory was tested and disproven — forcing
 * r-x on the linker did not change the failure.)
 *
 * A guest-loader open() redirect of /system/bin/linker64 → /system/bin/arm64/
 * linker64 makes init succeed, and with it ShadowHook's UNIQUE-mode inline
 * hooking works end to end under translation: this probe installs the hook, the
 * proxy fires, the chained original returns correctly, and unhook restores the
 * bytes (verified: "SHADOWHOOK OK"). So the translator DOES handle ShadowHook's
 * runtime code rewriting and dispatch. (An earlier belief that "executing a hook
 * SIGSEGVs in the trampoline" was a bug in THIS sample, not the translator: the
 * proxy wrongly used SHADOWHOOK_CALL_PREV/POP_STACK — the MULTI/SHARED-mode hub
 * mechanism — for a UNIQUE-mode hook, dereferencing a hub stack that is never
 * created in UNIQUE mode; that would fault on a real device too. Fixed to call
 * the saved orig directly.)
 *
 * The module is still out of the gate only because the linker redirect it needs
 * for init is not yet shipped: a real app that drives ShadowHook in SHARED mode
 * (NetEase Cloud Music) still regresses with the redirect, so it stays withheld
 * until that SHARED-mode path is resolved. This sample joins the gate then.
 *
 * ShadowHook installs hooks by rewriting ARM64 machine code at runtime: it
 * relocates a target function's prologue into a trampoline (fixing PC-relative
 * operands), overwrites the prologue with a branch to a proxy, and flushes the
 * instruction cache. That is guest self-modifying code operating on the
 * addresses of other translated guest code, which stresses the translator's
 * IC-invalidation and PC-relative-fixup paths far more than an ordinary
 * library-load probe.
 *
 * Flow:
 *   1. Initialize the ShadowHook runtime (UNIQUE mode) via its Java facade and
 *      confirm getInitErrno() == 0 (ERRNO_OK). init() loads libshadowhook.so and
 *      runs its native init in real translated ARM64 code.
 *   2. Load the companion libhelloshadowhook.so and call runHookProbe(), which
 *      installs a real inline hook on a local C++ function, calls it to prove
 *      the trampoline fires and the chained original still returns correctly,
 *      then unhooks and confirms restoration.
 *
 * It logs "SHADOWHOOK OK" or "SHADOWHOOK FAIL" so the suite's StatusTest can
 * assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    private external fun runHookProbe(): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Initialize ShadowHook's native runtime BEFORE any hook is
            // attempted. UNIQUE mode: each address may be hooked once, which is
            // exactly the single-hook lifecycle this probe drives.
            val config = ShadowHook.ConfigBuilder()
                .setMode(ShadowHook.Mode.UNIQUE)
                .setDebuggable(true)
                .build()
            ShadowHook.init(config)
            val initErrno = ShadowHook.getInitErrno()

            if (initErrno != 0) {  // 0 == ShadowHook ERRNO_OK
                "SHADOWHOOK FAIL: init errno=$initErrno"
            } else {
                // ShadowHook is live; load the companion library and run the
                // real inline-hook round-trip. The native side returns a string
                // beginning with "SHADOWHOOK OK" or "SHADOWHOOK FAIL".
                System.loadLibrary("helloshadowhook")
                runHookProbe()
            }
        } catch (t: Throwable) {
            "SHADOWHOOK FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloShadowhook"
    }
}
