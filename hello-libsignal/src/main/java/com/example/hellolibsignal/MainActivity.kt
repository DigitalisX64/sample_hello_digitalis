package com.example.hellolibsignal

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibsignal.R
import org.signal.libsignal.protocol.ecc.ECKeyPair
import org.signal.libsignal.protocol.kdf.HKDF

/**
 * Exercises Signal's libsignal — the Signal Protocol cryptography library —
 * under Berberis ARM64->x86_64 translation. The library's primitives are
 * implemented in Rust and shipped as the arm64-v8a libsignal_jni.so, which is
 * loaded on first use of any libsignal class.
 *
 * The probe runs entirely native crypto: it generates a curve25519/Ed25519 key
 * pair, signs a fixed message with the private key, verifies the signature with
 * the public key (must succeed), then verifies a tampered message against the
 * same signature (must fail). It additionally derives an HKDF secret twice and
 * checks the derivation is deterministic and non-trivial. It logs "LIBSIGNAL
 * OK" or "LIBSIGNAL FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runLibsignalProbe()
    }

    private fun runLibsignalProbe(): String {
        val msg: String = try {
            // --- Native curve25519/Ed25519 sign + verify (deterministic checks). ---
            val keyPair = ECKeyPair.generate()
            val privateKey = keyPair.privateKey
            val publicKey = keyPair.publicKey

            val message = "hello-libsignal deterministic probe".toByteArray(Charsets.UTF_8)
            val signature = privateKey.calculateSignature(message)

            // A valid signature over the original message must verify.
            val validVerifies = publicKey.verifySignature(message, signature)
            // A tampered message against the same signature must NOT verify.
            val tampered = message.copyOf().also { it[0] = (it[0] + 1).toByte() }
            val tamperRejected = !publicKey.verifySignature(tampered, signature)

            // --- Native HKDF derive: deterministic + non-trivial output. ---
            val ikm = ByteArray(32) { it.toByte() }
            val info = "hello-libsignal-hkdf".toByteArray(Charsets.UTF_8)
            val d1 = HKDF.deriveSecrets(ikm, info, 32)
            val d2 = HKDF.deriveSecrets(ikm, info, 32)
            val hkdfDeterministic = d1.contentEquals(d2)
            val hkdfNonTrivial = d1.size == 32 && d1.any { it.toInt() != 0 }

            val checks = listOf(
                "valid-verify" to validVerifies,
                "tamper-reject" to tamperRejected,
                "hkdf-deterministic" to hkdfDeterministic,
                "hkdf-nontrivial" to hkdfNonTrivial,
            )
            val failed = checks.filterNot { it.second }.map { it.first }
            if (failed.isEmpty()) {
                "LIBSIGNAL OK (curve25519 sign/verify + HKDF, pubKey=${publicKey.serialize().size}B, sig=${signature.size}B)"
            } else {
                "LIBSIGNAL FAIL: ${failed.joinToString(",")}"
            }
        } catch (t: Throwable) {
            Log.e(TAG, "probe threw", t)
            "LIBSIGNAL FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloLibsignal"
    }
}
