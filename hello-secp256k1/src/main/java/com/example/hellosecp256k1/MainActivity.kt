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

package com.example.hellosecp256k1

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.hellosecp256k1.R
import fr.acinq.secp256k1.Secp256k1

/**
 * Exercises libsecp256k1 — Bitcoin Core's optimized C library for elliptic-curve
 * operations on the secp256k1 curve — under Berberis ARM64->x86_64 translation.
 * The first Secp256k1 call loads the arm64-v8a libsecp256k1-jni.so; the probe
 * runs a full ECDSA round-trip plus an ECDH key agreement, all of which drive
 * 256-bit modular field arithmetic and EC scalar multiplication (UMULH-heavy
 * 64x64->128 multiplies), logging "SECP256K1 OK" or "SECP256K1 FAIL" so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runSecp256k1Probe()
        runBenchmarks()
    }

    /**
     * ECDSA over the Bitcoin curve: 256-bit modular arithmetic, so the hot loop
     * is full of 64x64->128 multiplies and carry chains — the integer path that
     * maps least directly onto the host, since ARM64 needs MUL+UMULH where
     * x86_64 has a single widening multiply and different flag semantics.
     */
    private fun runBenchmarks() {
        val module = "hello-secp256k1"
        val secp = Secp256k1.get()
        val privkey = ByteArray(32) { (it + 1).toByte() }
        val message = ByteArray(32) { (0xA0 + it).toByte() }
        val pubkey = secp.pubkeyCreate(privkey)
        val sig = secp.sign(message, privkey)

        // A single operation is ~100us, short enough that scheduling noise
        // dominates; 20 per iteration puts each measurement in the millisecond
        // range where the emulator is reliable.
        Bench.run(module, "sign-x20", warmup = 5, iters = 20) {
            repeat(20) { secp.sign(message, privkey) }
        }
        Bench.run(module, "verify-x20", warmup = 5, iters = 20) {
            repeat(20) { check(secp.verify(sig, message, pubkey)) }
        }
        Bench.run(module, "ecdh-x20", warmup = 3, iters = 15) {
            repeat(20) { secp.ecdh(privkey, pubkey) }
        }
        Bench.done(module)
    }

    private fun runSecp256k1Probe(): String {
        val msg = try {
            val secp = Secp256k1.get()

            // A fixed valid 32-byte private key (1 < d < curve order), a fixed
            // 32-byte message "hash" to sign, and a different 32-byte hash used
            // for the tamper check below.
            val privkey = ByteArray(32) { (it + 1).toByte() }
            val message = ByteArray(32) { (0xA0 + it).toByte() }
            val otherMessage = ByteArray(32) { (0x10 + it).toByte() }

            // EC scalar multiplication: derive the public key from the private key.
            val pubkey = secp.pubkeyCreate(privkey)

            // ECDSA: sign the message hash, then verify the signature.
            val sig = secp.sign(message, privkey)
            val verifiedReal = secp.verify(sig, message, pubkey)

            // Tamper check: the same signature must NOT verify against a
            // different message hash.
            val verifiedTampered = secp.verify(sig, otherMessage, pubkey)

            // ECDH: two keypairs must derive the same shared secret from each
            // other's public key.
            val privkeyB = ByteArray(32) { (it + 33).toByte() }
            val pubkeyB = secp.pubkeyCreate(privkeyB)
            val sharedAB = secp.ecdh(privkey, pubkeyB)
            val sharedBA = secp.ecdh(privkeyB, pubkey)
            val ecdhAgrees = sharedAB.contentEquals(sharedBA)

            if (verifiedReal && !verifiedTampered && ecdhAgrees) {
                "SECP256K1 OK (pubkey=${pubkey.size}B, sig=${sig.size}B, " +
                    "verify=true, tamper-rejected, ecdh=${sharedAB.size}B agree)"
            } else {
                "SECP256K1 FAIL: verifiedReal=$verifiedReal " +
                    "verifiedTampered=$verifiedTampered ecdhAgrees=$ecdhAgrees"
            }
        } catch (t: Throwable) {
            "SECP256K1 FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloSecp256k1"
    }
}
