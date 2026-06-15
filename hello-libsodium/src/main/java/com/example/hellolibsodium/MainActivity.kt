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

package com.example.hellolibsodium

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibsodium.R
import com.goterl.lazysodium.LazySodiumAndroid
import com.goterl.lazysodium.SodiumAndroid
import com.goterl.lazysodium.interfaces.GenericHash
import com.goterl.lazysodium.interfaces.SecretBox
import com.goterl.lazysodium.utils.Key

/**
 * Exercises libsodium (the NaCl C crypto library) under Berberis
 * ARM64->x86_64 translation, driven through Lazysodium-Android + JNA.
 * Constructing LazySodiumAndroid(SodiumAndroid()) loads the arm64-v8a
 * libsodium.so plus JNA's libjnidispatch.so, and every crypto call below is
 * JNA-marshalled into native libsodium code (heavy NEON/integer arithmetic).
 *
 * The probe runs two deterministic, self-checking crypto operations:
 *
 *  1. crypto_secretbox (XSalsa20-Poly1305) round-trip with a FIXED 32-byte key
 *     and FIXED 24-byte nonce: encrypt a known plaintext, then open it again,
 *     and verify the recovered plaintext exactly equals the original.
 *
 *  2. crypto_generichash (Blake2b-256, unkeyed) of the ASCII input "abc",
 *     verified against the well-known Blake2b-256 test vector
 *     BDDD813C634239723171EF3FEE98579B94964E3BB1CB3E427262C8C068D52319
 *     (compared case-insensitively).
 *
 * Logs "SODIUM OK" on success or "SODIUM FAIL: <reason>" on any failure, so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Loads arm64-v8a libsodium.so + libjnidispatch.so on first use.
            val ls = LazySodiumAndroid(SodiumAndroid())

            // --- 1. crypto_secretbox round-trip (fixed key + fixed nonce) ---
            // Fixed 32-byte key and 24-byte nonce make the whole exchange
            // deterministic, so a correct translation must recover the exact
            // original plaintext.
            val key = Key.fromBytes(ByteArray(SecretBox.KEYBYTES) { (it + 1).toByte() })
            val nonce = ByteArray(SecretBox.NONCEBYTES) { (0x40 + it).toByte() }
            val plaintext = "Digitalis loves libsodium"

            val cipherHex = ls.cryptoSecretBoxEasy(plaintext, nonce, key)
            val recovered = ls.cryptoSecretBoxOpenEasy(cipherHex, nonce, key)
            val secretBoxOk = recovered == plaintext

            // --- 2. crypto_generichash (Blake2b-256) against a known vector ---
            val hashHex = ls.cryptoGenericHash("abc")
            val hashOk = hashHex.equals(BLAKE2B_ABC_HEX, ignoreCase = true)

            if (secretBoxOk && hashOk) {
                "SODIUM OK (secretbox round-trip recovered ${recovered.length} chars; " +
                    "blake2b-256(\"abc\") matched vector)"
            } else {
                "SODIUM FAIL: secretBoxOk=$secretBoxOk hashOk=$hashOk " +
                    "(recovered=\"$recovered\" hash=$hashHex)"
            }
        } catch (t: Throwable) {
            "SODIUM FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloLibsodium"

        // Blake2b-256 (unkeyed, 32-byte output) of the ASCII bytes "abc".
        private const val BLAKE2B_ABC_HEX =
            "BDDD813C634239723171EF3FEE98579B94964E3BB1CB3E427262C8C068D52319"
    }
}
