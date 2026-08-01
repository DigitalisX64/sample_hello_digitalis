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
package com.example.hellopcre2

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.hellopcre2.R

/**
 * Exercises PCRE2 (Perl-Compatible Regular Expressions) and its sljit-based JIT
 * under Berberis ARM64->x86_64 translation.
 *
 * The native probe (libhellopcre2.so, which links the prefab libpcre2-8.so)
 * compiles a date pattern, JIT-compiles it with PCRE2_JIT_COMPLETE, then matches
 * a subject string two ways: pcre2_jit_match (runs the JIT-generated ARM64
 * machine code) and pcre2_match (the bytecode interpreter). PCRE2's JIT emits
 * code at runtime and announces self-modified code via IC IVAU, so this directly
 * exercises the translator's self-modifying-code invalidation path; the probe
 * then checks both paths capture the same three groups. It logs "PCRE2 OK ..."
 * or "PCRE2 FAIL: ..." so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    private external fun runProbe(): String

    /** Repeated matching; use_jit selects PCRE2's own runtime code generator. */
    private external fun benchMatch(reps: Int, useJit: Boolean): Int

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val msg = try {
            // The native string already carries the OK/FAIL verdict; just relay.
            runProbe()
        } catch (t: Throwable) {
            "PCRE2 FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        findViewById<TextView>(R.id.sample_text).text = msg
        runBenchmarks()
    }

    /**
     * The same 2000 matches twice: once through PCRE2's JIT, once through its
     * interpreter. The JIT case is guest code generating guest code, so it also
     * exercises the translator's self-modifying-code invalidation.
     */
    private fun runBenchmarks() {
        val module = "hello-pcre2"
        val reps = 2000
        Bench.run(module, "match-jit-2000", warmup = 3, iters = 15) {
            check(benchMatch(reps, true) == reps)
        }
        Bench.run(module, "match-interp-2000", warmup = 3, iters = 15) {
            check(benchMatch(reps, false) == reps)
        }
        Bench.done(module)
    }

    companion object {
        private const val TAG = "HelloPcre2"

        init {
            System.loadLibrary("hellopcre2")
        }
    }
}
