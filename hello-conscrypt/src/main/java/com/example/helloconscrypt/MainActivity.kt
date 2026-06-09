package com.example.helloconscrypt

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloconscrypt.R
import org.conscrypt.Conscrypt
import java.security.MessageDigest
import javax.crypto.Cipher
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * Exercises Conscrypt — Google's JCA provider backed by native BoringSSL — under
 * Berberis ARM64->x86_64 translation. The arm64-v8a libconscrypt_jni.so runs the
 * SHA-256 and AES-GCM here. Checks SHA-256("abc") against its known digest and an
 * AES-GCM encrypt/decrypt round-trip, both forced through the Conscrypt provider,
 * and logs "CONSCRYPT OK" / "CONSCRYPT FAIL" for the suite's StatusTest.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        return try {
            val provider = Conscrypt.newProvider()

            // SHA-256("abc") has a well-known digest.
            val md = MessageDigest.getInstance("SHA-256", provider)
            val digest = md.digest("abc".toByteArray(Charsets.US_ASCII))
            val hex = digest.joinToString("") { "%02x".format(it) }
            val expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
            val shaOk = hex == expected
            val providerOk = md.provider.name.contains("Conscrypt", ignoreCase = true)

            // AES-128-GCM encrypt then decrypt round-trip.
            val key = SecretKeySpec(ByteArray(16) { it.toByte() }, "AES")
            val iv = ByteArray(12) { (it * 7).toByte() }
            val plaintext = "conscrypt-boringssl-ⓦ".toByteArray(Charsets.UTF_8)
            val enc = Cipher.getInstance("AES/GCM/NoPadding", provider)
            enc.init(Cipher.ENCRYPT_MODE, key, GCMParameterSpec(128, iv))
            val ciphertext = enc.doFinal(plaintext)
            val dec = Cipher.getInstance("AES/GCM/NoPadding", provider)
            dec.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, iv))
            val roundTrip = dec.doFinal(ciphertext)
            val gcmOk = roundTrip.contentEquals(plaintext) && !ciphertext.contentEquals(plaintext)

            val checks = listOf(
                "sha256-abc" to shaOk,
                "conscrypt-provider" to providerOk,
                "aes-gcm-roundtrip" to gcmOk,
            )
            val failed = checks.filterNot { it.second }.map { it.first }
            val msg = if (failed.isEmpty()) {
                "CONSCRYPT OK (provider=${md.provider.name}, sha256=${hex.take(12)}.., " +
                    "gcm=${ciphertext.size}B)"
            } else {
                "CONSCRYPT FAIL: ${failed.joinToString(",")} (sha256=$hex)"
            }
            Log.i(TAG, msg)
            msg
        } catch (t: Throwable) {
            val msg = "CONSCRYPT FAIL: exception ${t.javaClass.simpleName}: ${t.message}"
            Log.e(TAG, msg, t)
            msg
        }
    }

    companion object {
        private const val TAG = "HelloConscrypt"
    }
}
