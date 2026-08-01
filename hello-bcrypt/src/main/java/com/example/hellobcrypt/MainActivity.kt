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
package com.example.hellobcrypt

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.hellobcrypt.R

/**
 * bcrypt password hashing under Berberis ARM64->x86_64 translation, against the
 * real library: Openwall's crypt_blowfish with the libbcrypt wrapper that
 * provides bcrypt_gensalt / bcrypt_hashpw / bcrypt_checkpw. Both are vendored
 * under cpp/ and built for arm64-v8a, so the translator sees the same code an
 * app ships when it signs requests with a bcrypt token.
 *
 * bcrypt fails silently: a translator bug does not crash, it returns a
 * different digest, and the app reports a wrong password. The probe therefore
 * checks answers rather than liveness — the 28 known-answer vectors from
 * crypt_blowfish's own self-test table, bcrypt_checkpw accepting the right
 * password and rejecting a wrong one, the settings the library must refuse, and
 * a gensalt/hashpw/checkpw round trip at cost 12 whose 4096 key-schedule
 * iterations take the hot loop past the two-gear optimizer's gear-up threshold.
 *
 * It logs "BCRYPT OK ..." or "BCRYPT FAIL: ..." so the suite's StatusTest can
 * assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    private external fun runProbe(): String

    /** One hash at the given cost — the timed workload. */
    private external fun benchHashOnce(cost: Int): Int

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val msg = try {
            // The native string already carries the OK/FAIL verdict; just relay.
            runProbe()
        } catch (t: Throwable) {
            "BCRYPT FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        findViewById<TextView>(R.id.sample_text).text = msg
        runBenchmarks()
    }

    /**
     * Timed workloads. Cost 8 keeps an iteration in the millisecond range while
     * still running 256 key-schedule rounds — long enough for a region to go
     * hot and the second gear to engage, short enough for 30 iterations.
     * Cost 10 is the same work scaled 4x, as a check that the ratio holds.
     */
    private fun runBenchmarks() {
        val module = "hello-bcrypt"
        Bench.run(module, "hashpw-cost8") { check(benchHashOnce(8) == 0) }
        Bench.run(module, "hashpw-cost10", warmup = 3, iters = 15) {
            check(benchHashOnce(10) == 0)
        }
        Bench.done(module)
    }

    companion object {
        private const val TAG = "HelloBcrypt"

        init {
            System.loadLibrary("hellobcrypt")
        }
    }
}
