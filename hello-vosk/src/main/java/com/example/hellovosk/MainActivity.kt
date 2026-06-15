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
package com.example.hellovosk

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellovosk.R
import org.vosk.LibVosk
import org.vosk.LogLevel

/**
 * Exercises Vosk — the offline Kaldi-based speech-recognition engine — under
 * Berberis ARM64->x86_64 translation.
 *
 * Vosk's Java binding (org.vosk.LibVosk) reaches the arm64-v8a libvosk.so
 * (~8.8 MB of Kaldi/OpenFST/BLAS native code) through JNA direct mapping
 * (Native.register), which in turn loads JNA's own arm64-v8a
 * libjnidispatch.so. Full transcription needs a ~40 MB acoustic model on disk,
 * which is far too large to embed in a sample APK, so this is a native-load
 * plus model-free smoke test: the first LibVosk call links libvosk.so and
 * libjnidispatch.so into the process, and LibVosk.setLogLevel(...) makes a real
 * JNI/JNA round trip into Kaldi's native vosk_set_log_level() — no model
 * required. Driving every LogLevel value confirms the native entry point runs
 * and returns cleanly across the translation boundary, logging "VOSK OK" or
 * "VOSK FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // The first setLogLevel call resolves and loads the arm64-v8a
            // libvosk.so (and JNA's libjnidispatch.so), then dispatches into
            // Kaldi's native vosk_set_log_level() through JNA. This is a real
            // native round trip that needs no acoustic model. Drive every
            // LogLevel so the native path is exercised repeatedly; if any call
            // faults or throws, the catch below turns it into a VOSK FAIL.
            var calls = 0
            for (level in LogLevel.values()) {
                LibVosk.setLogLevel(level)
                calls++
            }
            // Leave logging quiet for the rest of the run.
            LibVosk.setLogLevel(LogLevel.WARNINGS)
            calls++

            if (calls == LogLevel.values().size + 1) {
                "VOSK OK ($calls vosk_set_log_level calls into libvosk.so, " +
                    "model-free native smoke test)"
            } else {
                // Unreachable in practice (a failed native call throws), but
                // keep an explicit self-check so a silently-skipped loop is
                // surfaced rather than masquerading as success.
                "VOSK FAIL: only $calls native call(s) completed"
            }
        } catch (t: Throwable) {
            "VOSK FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloVosk"
    }
}
