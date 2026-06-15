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
package com.example.hellothemis

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.cossacklabs.themis.SecureCell
import com.example.hellodigitalis.hellothemis.R

/**
 * Exercises Themis crypto (Cossack Labs) under Berberis ARM64->x86_64
 * translation. Themis is a native (C) cryptography library built on BoringSSL;
 * the first SecureCell call loads the arm64-v8a libthemis_jni.so (libthemis +
 * libsoter + BoringSSL statically linked, JNI bridged), so the entire AES/HMAC
 * encrypt/decrypt path runs as translated ARM64 native code.
 *
 * The probe builds a passphrase-derived Secure Cell, encrypts a known
 * plaintext, decrypts the ciphertext, and self-checks that the round-trip is
 * lossless AND that the ciphertext actually differs from the plaintext (real
 * encryption happened), logging "THEMIS OK" or "THEMIS FAIL" so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            val plaintext = "hello digitalis".toByteArray()

            // Passphrase-derived Secure Cell (Seal mode). The native rounds run
            // KDF + AES-256-GCM + HMAC entirely inside libthemis_jni.so.
            val cell = SecureCell.SealWithPassphrase("digitalis-pass")
            val enc = cell.encrypt(plaintext)
            val dec = cell.decrypt(enc)

            val roundTripOk = String(dec) == "hello digitalis"
            val didEncrypt = !enc.contentEquals(plaintext)

            if (roundTripOk && didEncrypt) {
                "THEMIS OK (SecureCell seal, ${plaintext.size}->${enc.size} bytes)"
            } else {
                "THEMIS FAIL: roundTripOk=$roundTripOk didEncrypt=$didEncrypt " +
                    "(${plaintext.size}->${enc.size} bytes)"
            }
        } catch (t: Throwable) {
            "THEMIS FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloThemis"
    }
}
