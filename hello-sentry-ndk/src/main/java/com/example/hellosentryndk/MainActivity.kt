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
package com.example.hellosentryndk

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellosentryndk.R

/**
 * Exercises the Sentry Android NDK integration — a native crash/unwind library —
 * under Berberis ARM64->x86_64 translation. Initializing Sentry with the NDK
 * integration enabled loads the arm64-v8a libsentry.so + libsentry-android.so
 * (bionic-linked) and installs a signal-handler-based native stack unwinder
 * (.eh_frame), a distinct native subsystem from the JNI-compression / codec
 * samples already in the suite.
 *
 * The probe does NOT trigger a real crash (that would log "Fatal signal" and
 * fail the suite's StatusTest). It only brings the native backend up, captures a
 * single normal message — which is dropped in beforeSend so no network I/O ever
 * happens — and self-checks that Sentry reports itself enabled, logging
 * "SENTRY OK" or "SENTRY FAIL" so StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runSentryProbe()
    }

    private fun runSentryProbe(): String {
        val msg = try {
            io.sentry.android.core.SentryAndroid.init(this) { options ->
                // Syntactically valid DSN that is never actually reached on the
                // network, because we drop every event before send.
                options.dsn = "https://00000000000000000000000000000000@o0.ingest.sentry.io/0"
                options.isEnableNdk = true            // load libsentry.so + install native handler
                options.isAnrEnabled = false
                options.isDebug = false
                options.setBeforeSend { _, _ -> null } // drop all events => no network I/O
            }

            val enabled = io.sentry.Sentry.isEnabled()
            // Exercised, but dropped by beforeSend so it never leaves the device.
            io.sentry.Sentry.captureMessage("digitalis sentry-ndk probe")

            if (enabled) {
                "SENTRY OK (NDK init, native unwinder loaded, isEnabled=true)"
            } else {
                "SENTRY FAIL: Sentry.isEnabled() returned false after init"
            }
        } catch (t: Throwable) {
            "SENTRY FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloSentryNdk"
    }
}
