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
package com.example.helloblowfish

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloblowfish.R

/**
 * Blowfish / bcrypt correctness probe under Berberis ARM64->x86_64 translation.
 *
 * The native probe (libhelloblowfish.so) implements Blowfish from the published
 * specification — canonical pi-derived P-array and S-boxes, so it is
 * bit-compatible with the real cipher — and drives it through the exact ARM64
 * shape that password-hashing code compiles to: an unrolled 16-round chain of
 * four S-box lookups (ubfiz / lsr + and #0x3fc / indexed ldr) wrapped in a
 * bcrypt-style expensive key schedule whose loop stores its results back
 * (stp) into the same tables the round chain loads from, across a backward
 * branch. A miscompile of that pattern in any translation tier produces a wrong
 * hash and no crash, which real apps surface only as a failed login.
 *
 * The probe checks eight published Blowfish ECB known-answer vectors (encrypt
 * and decrypt), pins a bcrypt cost-10 digest to a golden value, recomputes that
 * digest to catch a nondeterministic miscompile, and confirms a one-bit key
 * change diverges. It logs "BLOWFISH OK ..." or "BLOWFISH FAIL: ..." so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    private external fun runProbe(): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val msg = try {
            // The native string already carries the OK/FAIL verdict; just relay.
            runProbe()
        } catch (t: Throwable) {
            "BLOWFISH FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        findViewById<TextView>(R.id.sample_text).text = msg
    }

    companion object {
        private const val TAG = "HelloBlowfish"

        init {
            System.loadLibrary("helloblowfish")
        }
    }
}
