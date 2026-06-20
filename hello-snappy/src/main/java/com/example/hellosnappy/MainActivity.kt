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

package com.example.hellosnappy

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellosnappy.R
import org.xerial.snappy.Snappy

/**
 * Exercises Snappy (Google's Snappy compression) via snappy-java's JNI bindings
 * — a native (C++) library — under Berberis ARM64->x86_64 translation. The first
 * Snappy call self-extracts and loads the arm64-v8a libsnappyjava.so; the probe
 * compresses a known 4 KB repeating-pattern buffer, decompresses it, and
 * self-checks that the round-trip is lossless AND that the compressed form is
 * actually smaller than the original (compression happened). It then round-trips
 * a String through Snappy.compress(String)/Snappy.uncompressString and checks
 * Snappy.isValidCompressedBuffer reports the compressed bytes valid, logging
 * "SNAPPY OK" or "SNAPPY FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runSnappyProbe()
    }

    private fun runSnappyProbe(): String {
        // snappy-java normally self-extracts its bundled native by inspecting
        // os.arch. Under Berberis native bridge the managed runtime is the host
        // (os.arch=x86_64) while native code must be the arm64 guest, so that
        // self-selection picks the wrong/absent native. Instead we deliver the
        // arm64 libsnappyjava.so (+ its libc++_shared.so) through the APK's
        // jniLibs (see build.gradle.kts) and tell snappy to load it via
        // System.loadLibrary, which the guest linker resolves and Berberis
        // translates. Must be set before the first Snappy call.
        System.setProperty("org.xerial.snappy.use.systemlib", "true")
        val msg = try {
            // A 4 KB buffer of a short repeating pattern: highly compressible,
            // so a working Snappy must shrink it well below 4096 bytes.
            val original = ByteArray(4096) { (it % 251).toByte() }

            val compressed = Snappy.compress(original)
            val decompressed = Snappy.uncompress(compressed)

            val roundTripOk = decompressed.contentEquals(original)
            val didCompress = compressed.size < original.size
            val validBuffer = Snappy.isValidCompressedBuffer(compressed)

            // Also drive the String compress/uncompress (UTF-8) code path.
            val text = "Snappy under Berberis ARM64->x86_64. ".repeat(64)
            val textCompressed = Snappy.compress(text)
            val textRoundTrip = Snappy.uncompressString(textCompressed)
            val stringOk = textRoundTrip == text

            if (roundTripOk && didCompress && validBuffer && stringOk) {
                "SNAPPY OK (${original.size}->${compressed.size} bytes; " +
                    "string ${text.length}->${textCompressed.size} bytes; " +
                    "valid=$validBuffer)"
            } else {
                "SNAPPY FAIL: roundTripOk=$roundTripOk didCompress=$didCompress " +
                    "validBuffer=$validBuffer stringOk=$stringOk " +
                    "(${original.size}->${compressed.size} bytes)"
            }
        } catch (t: Throwable) {
            "SNAPPY FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloSnappy"
    }
}
