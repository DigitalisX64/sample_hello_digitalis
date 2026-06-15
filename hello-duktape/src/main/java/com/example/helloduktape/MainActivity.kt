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

package com.example.helloduktape

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloduktape.R
import com.squareup.duktape.Duktape

/**
 * Exercises Duktape — an embeddable JavaScript engine written in C — under
 * Berberis ARM64->x86_64 translation. Creating the context loads the
 * arm64-v8a libduktape.so; the probe then runs two JS snippets through
 * Duktape's bytecode interpreter (a computed-goto dispatch loop with heavy
 * pointer chasing): an arithmetic loop summing 1..100 and an array join,
 * self-checking both results so the suite's StatusTest can assert a clean run.
 * Logs "DUKTAPE OK" on success or "DUKTAPE FAIL: <reason>" on any failure.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            val dt = Duktape.create()
            try {
                // Arithmetic loop: 1+2+...+100 = 5050. Drives the interpreter's
                // integer add / compare / branch dispatch many thousands of times.
                val sum = dt.evaluate(
                    "var s = 0; for (var i = 1; i <= 100; i++) s += i; s.toString();"
                ) as String
                // Array allocation + string join — pointer-heavy heap work.
                val joined = dt.evaluate("['a','b','c'].join('-')") as String

                val sumOk = sum == "5050"
                val joinOk = joined == "a-b-c"

                if (sumOk && joinOk) {
                    "DUKTAPE OK (sum=$sum, join=$joined)"
                } else {
                    "DUKTAPE FAIL: sumOk=$sumOk (got '$sum') " +
                        "joinOk=$joinOk (got '$joined')"
                }
            } finally {
                dt.close()
            }
        } catch (t: Throwable) {
            "DUKTAPE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloDuktape"
    }
}
