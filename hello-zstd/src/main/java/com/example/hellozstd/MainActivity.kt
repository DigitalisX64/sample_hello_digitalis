package com.example.hellozstd

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellozstd.R
import com.github.luben.zstd.Zstd

/**
 * Exercises Zstandard (zstd) JNI — a native (C) compression library — under
 * Berberis ARM64->x86_64 translation. The first Zstd call loads the arm64-v8a
 * libzstd-jni-*.so; the probe compresses a known 4 KB repeating-pattern buffer,
 * decompresses it, and self-checks that the round-trip is lossless AND that the
 * compressed form is actually smaller than the original (compression happened),
 * logging "ZSTD OK" or "ZSTD FAIL" so the suite's StatusTest can assert a clean
 * run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runZstdProbe()
    }

    private fun runZstdProbe(): String {
        val msg = try {
            // A 4 KB buffer of a short repeating pattern: highly compressible,
            // so a working zstd must shrink it well below 4096 bytes.
            val original = ByteArray(4096) { (it % 251).toByte() }

            val compressed = Zstd.compress(original, 3)
            val decompressed = Zstd.decompress(compressed, original.size)

            val roundTripOk = decompressed.contentEquals(original)
            val didCompress = compressed.size < original.size

            if (roundTripOk && didCompress) {
                "ZSTD OK (${original.size}->${compressed.size} bytes, level 3)"
            } else {
                "ZSTD FAIL: roundTripOk=$roundTripOk didCompress=$didCompress " +
                    "(${original.size}->${compressed.size} bytes)"
            }
        } catch (t: Throwable) {
            "ZSTD FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloZstd"
    }
}
