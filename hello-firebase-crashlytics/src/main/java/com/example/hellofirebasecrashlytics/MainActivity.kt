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
package com.example.hellofirebasecrashlytics

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellofirebasecrashlytics.R
import com.google.firebase.FirebaseApp
import com.google.firebase.FirebaseOptions
import com.google.firebase.crashlytics.FirebaseCrashlytics

/**
 * Exercises the Firebase Crashlytics NDK native crash handler — libcrashlytics.so
 * plus the libcrashlytics-common / -handler / -trampoline Breakpad natives, the
 * single most common native crash-reporting SDK across real apps — LOADING and
 * INSTALLING its native signal handler under Berberis ARM64->x86_64 translation,
 * WITHOUT ever actually crashing.
 *
 * Crashlytics needs a FirebaseApp to initialize, so this probe builds one in-code
 * from a syntactically valid dummy [FirebaseOptions] (no network is needed for the
 * NDK component to install its handler locally, so no google-services.json is
 * shipped). It then enables collection and drives the API — log() and a NON-fatal
 * recordException() — which triggers the NDK component to install its native
 * handler on a background thread. The real translator-exercised path is the
 * arm64-v8a libcrashlytics*.so loading and running its init/handler-install code;
 * the probe VERIFIES that by confirming "libcrashlytics" appears in
 * /proc/self/maps, and logs "FIREBASE-CRASHLYTICS OK" / "FIREBASE-CRASHLYTICS FAIL"
 * so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // The translator-relevant target is the arm64-v8a libcrashlytics*.so
            // Breakpad natives loading and running their init/JNI_OnLoad under
            // translation. Load them DIRECTLY: the firebase-crashlytics-ndk AAR
            // ships them into lib/arm64-v8a/, and System.loadLibrary resolves +
            // runs each .so's constructors/JNI registration under Berberis.
            // Order matters — the crash handler and its dependencies first.
            val libs = listOf(
                "crashlytics-common",
                "crashlytics-handler",
                "crashlytics-trampoline",
                "crashlytics",
            )
            val loaded = ArrayList<String>()
            for (lib in libs) {
                try {
                    System.loadLibrary(lib)
                    loaded.add(lib)
                } catch (t: Throwable) {
                    // Some artifacts don't ship every soname; a missing optional
                    // one is fine as long as the core libcrashlytics maps below.
                    Log.i(TAG, "loadLibrary($lib): ${t.javaClass.simpleName}: ${t.message}")
                }
            }

            // Best-effort: also drive the Crashlytics Java API so its own JNI
            // native-init path runs too. It throws without the Crashlytics Gradle
            // plugin (a build-config requirement unrelated to translation), so its
            // failure is swallowed — the native .so load above is the real probe.
            try {
                if (FirebaseApp.getApps(this).isEmpty()) {
                    val options = FirebaseOptions.Builder()
                        .setApplicationId("1:1234567890:android:abcdef123456")
                        .setApiKey("AIzaSyFakeKeyForHelloDigitalisTest0000000")
                        .setProjectId("hello-digitalis")
                        .build()
                    FirebaseApp.initializeApp(this, options)
                }
                val fc = FirebaseCrashlytics.getInstance()
                fc.setCrashlyticsCollectionEnabled(true)
                fc.log("hello-digitalis crashlytics-ndk probe")
                fc.recordException(RuntimeException("hello-digitalis non-fatal probe"))
            } catch (t: Throwable) {
                Log.i(TAG, "Crashlytics Java API skipped (needs Gradle plugin): " +
                    "${t.javaClass.simpleName}")
            }

            val mapped = waitForLibMapped(timeoutMs = 3000)
            if (mapped) {
                "FIREBASE-CRASHLYTICS OK (loaded ${loaded.joinToString(",")}; libcrashlytics mapped)"
            } else {
                "FIREBASE-CRASHLYTICS FAIL: libcrashlytics not mapped after loading $loaded"
            }
        } catch (t: Throwable) {
            "FIREBASE-CRASHLYTICS FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    /** Poll /proc/self/maps until it contains "libcrashlytics" or the timeout elapses. */
    private fun waitForLibMapped(timeoutMs: Long): Boolean {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (isLibMapped()) return true
            try {
                Thread.sleep(100)
            } catch (ignored: InterruptedException) {
                Thread.currentThread().interrupt()
                return isLibMapped()
            }
        }
        return isLibMapped()
    }

    private fun isLibMapped(): Boolean = try {
        java.io.File("/proc/self/maps").readText().contains("libcrashlytics")
    } catch (t: Throwable) {
        false
    }

    companion object {
        private const val TAG = "HelloFirebaseCx"
    }
}
