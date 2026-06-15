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
package com.example.hellowireguard

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellowireguard.R
import com.wireguard.android.util.SharedLibraryLoader

/**
 * Exercises the WireGuard Android tunnel's native backend — wireguard-go,
 * shipped as arm64-v8a libwg-go.so — under Berberis ARM64->x86_64 translation.
 *
 * Bringing up a real tunnel needs the system VPN permission (and/or root), so
 * it is out of scope for a headless probe. But the native version entry point
 * is callable headlessly: this probe loads libwg-go.so via the tunnel AAR's
 * public SharedLibraryLoader.loadSharedLibrary(context, "wg-go"), then invokes
 * GoBackend.wgVersion() (a private static native, reached via reflection). That
 * native call spins up the full Go runtime — goroutine scheduler, Go signal
 * handling, cgo bridge — entirely under translation, and returns the
 * wireguard-go version string. The probe self-checks that the returned string
 * is a non-empty, printable token (tagged releases look like "0.0.2023…" while
 * module builds report a git revision such as "f333402"), logging
 * "WIREGUARD OK" or "WIREGUARD FAIL" so the suite's StatusTest can assert a
 * clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Load arm64-v8a libwg-go.so through the tunnel AAR's loader. The
            // GoBackend constructor normally does this, but it also requires a
            // VpnService context; the loader itself is public and standalone.
            SharedLibraryLoader.loadSharedLibrary(this, "wg-go")

            // wgVersion() is a private static native String method on
            // com.wireguard.android.backend.GoBackend. Reach it via reflection
            // (no public accessor exists) — this is the headless native entry
            // point that runs the wireguard-go Go runtime under translation.
            val goBackend = Class.forName("com.wireguard.android.backend.GoBackend")
            val wgVersion = goBackend.getDeclaredMethod("wgVersion").apply {
                isAccessible = true
            }
            val version = wgVersion.invoke(null) as? String

            // The success criterion is simply that wgVersion() returned from the
            // Go runtime under translation with a non-empty version string. The
            // string's exact shape varies by build: tagged releases look like
            // "0.0.20230223" / "go1.x" / "v0.x", but module builds report the
            // wireguard-go git revision (e.g. "f333402"), so we accept any
            // non-empty, reasonably short, all-printable token rather than
            // assuming a leading digit / "go" / "v".
            val v = version?.trim()
            val versionOk = !v.isNullOrEmpty() && v.length <= 64 &&
                v.all { it.code in 0x21..0x7e }

            if (versionOk) {
                "WIREGUARD OK (wireguard-go version=\"$v\", libwg-go.so loaded, Go runtime ran)"
            } else {
                "WIREGUARD FAIL: unexpected version string version=\"$version\""
            }
        } catch (t: Throwable) {
            "WIREGUARD FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloWireguard"
    }
}
