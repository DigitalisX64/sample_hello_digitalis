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
 *
 * KNOWN LIMITATION (why this module is NOT in the test-samples.sh gate):
 * ShadowHook.init() fails with SHADOWHOOK_ERRNO_INIT_LINKER (12) under Berberis.
 * Its init analyzes the dynamic linker (to monitor dlopen) by walking the
 * linker's executable code segment, but Berberis maps ALL guest code — including
 * /system/bin/arm64/linker64 — as read-only (r--p) and JIT-translates it, so no
 * executable (r-xp) linker segment exists for the engine to introspect. This is
 * an architectural characteristic of the translator, not a bug in this sample;
 * the sample is kept as a buildable reference and will pass once the guest
 * loader exposes guest .text so hooking engines can init.
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
