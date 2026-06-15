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
package com.example.helloargon2

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloargon2.R
import com.lambdapioneer.argon2kt.Argon2Kt
import com.lambdapioneer.argon2kt.Argon2Mode

/**
 * Exercises Argon2Kt — Kotlin bindings for the native Argon2 password-hashing
 * library (Password Hashing Competition winner) — under Berberis
 * ARM64->x86_64 translation. Constructing Argon2Kt loads the arm64-v8a
 * libargon2jni.so + libargon2native.so; the probe then runs the memory-hard
 * Argon2id KDF over a 64 MiB working buffer (3 iterations), self-checks that
 * the PHC-encoded output starts with "$argon2id$", that verifying the correct
 * password succeeds, and that verifying a wrong password fails — logging
 * "ARGON2 OK" or "ARGON2 FAIL" so the suite's StatusTest can assert a clean
 * run. The 64 MiB fill makes this a heavy translator memory-traffic stress.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            val password = "password".toByteArray()
            val wrongPassword = "wrong-password".toByteArray()
            // Argon2 requires an at-least-8-byte salt; this is exactly 16 bytes.
            val salt = "somesalt16bytes!".toByteArray()

            // First touch of Argon2Kt dlopen()s the native libraries.
            val argon2 = Argon2Kt()

            // Memory-hard hash: 3 passes over a 64 MiB (65536 KiB) buffer.
            val result = argon2.hash(
                mode = Argon2Mode.ARGON2_ID,
                password = password,
                salt = salt,
                tCostInIterations = 3,
                mCostInKibibyte = 65536,
            )
            val encoded = result.encodedOutputAsString()

            val encodedOk = encoded.startsWith("\$argon2id\$")
            val verifyCorrect = argon2.verify(
                mode = Argon2Mode.ARGON2_ID,
                encoded = encoded,
                password = password,
            )
            val verifyWrong = argon2.verify(
                mode = Argon2Mode.ARGON2_ID,
                encoded = encoded,
                password = wrongPassword,
            )

            if (encodedOk && verifyCorrect && !verifyWrong) {
                "ARGON2 OK (argon2id t=3 m=64MiB, ${encoded.length}-char PHC string, " +
                    "verify-correct=true verify-wrong=false)"
            } else {
                "ARGON2 FAIL: encodedOk=$encodedOk verifyCorrect=$verifyCorrect " +
                    "verifyWrong=$verifyWrong encoded=$encoded"
            }
        } catch (t: Throwable) {
            "ARGON2 FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloArgon2"
    }
}
