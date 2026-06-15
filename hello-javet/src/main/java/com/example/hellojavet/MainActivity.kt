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
package com.example.hellojavet

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.caoccao.javet.interop.V8Host
import com.caoccao.javet.interop.V8Runtime
import com.example.hellodigitalis.hellojavet.R

/**
 * Exercises Javet — the Java + Google V8 JavaScript engine binding — under
 * Berberis ARM64->x86_64 translation. The first V8Host call loads the
 * arm64-v8a libjavet-v8-android.*.so JNI bridge and the bundled V8 runtime.
 * The probe creates a V8 runtime and runs two scripts:
 *
 *   1. An arithmetic loop (`for (i=1..100) s+=i`) executed thousands of times
 *      so V8's optimizing compilers (Sparkplug / TurboFan) tier the bytecode
 *      up to native arm64 machine code and re-patch inline caches in place —
 *      i.e. self-modifying code that depends on the translator honouring
 *      IC IVAU as a translation-cache invalidation rather than a NOP. The
 *      result must equal 5050.
 *   2. A string-returning script, to confirm V8<->JNI value marshalling of a
 *      non-numeric result.
 *
 * It self-checks both results and logs "JAVET OK" or "JAVET FAIL: <reason>"
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
            V8Host.getV8Instance().createV8Runtime<V8Runtime>().use { rt ->
                // Run the summation loop many times. Repeated execution drives
                // V8 to JIT-compile and then re-optimize the hot loop into
                // native arm64 code, exercising self-modifying code / IC IVAU.
                var sum = 0
                repeat(WARMUP_ITERATIONS) {
                    sum = rt.getExecutor(SUM_SCRIPT).executeInteger()
                }

                // A string-returning script: checks V8<->JNI String marshalling.
                val greeting = rt.getExecutor(STRING_SCRIPT).executeString()

                val sumOk = sum == EXPECTED_SUM
                val stringOk = greeting == EXPECTED_STRING

                if (sumOk && stringOk) {
                    "JAVET OK (sum=$sum after $WARMUP_ITERATIONS runs, str=\"$greeting\")"
                } else {
                    "JAVET FAIL: sumOk=$sumOk (sum=$sum) " +
                        "stringOk=$stringOk (str=\"$greeting\")"
                }
            }
        } catch (t: Throwable) {
            "JAVET FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloJavet"

        // Enough repetitions to push V8 past its tier-up thresholds so the hot
        // loop is recompiled to native arm64 machine code at least once.
        private const val WARMUP_ITERATIONS = 5000

        private const val SUM_SCRIPT = "var s = 0; for (var i = 1; i <= 100; i++) s += i; s"
        private const val EXPECTED_SUM = 5050

        private const val STRING_SCRIPT =
            "['Hello', 'V8', 'on', 'Digitalis'].join(' ')"
        private const val EXPECTED_STRING = "Hello V8 on Digitalis"
    }
}
