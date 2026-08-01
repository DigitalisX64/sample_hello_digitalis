package com.example.hellozstd

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
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
        runBenchmarks()
    }

    /**
     * Timed workloads, separate from the correctness probe above.
     *
     * 1 MB per iteration so the measurement is dominated by the codec rather
     * than by JNI call overhead, and semi-compressible rather than a repeating
     * pattern — a 4 KB run of one byte compresses to nothing and would measure
     * the early-exit path instead of the match finder. Levels 3 and 9 bracket
     * the range apps actually use; decompression is timed separately because it
     * is a different code path with a very different instruction mix.
     */
    private fun runBenchmarks() {
        val module = "hello-zstd"
        val data = ByteArray(1 shl 20) { ((it * 31) xor (it shr 5)).toByte() }
        val compressed = Zstd.compress(data, 3)

        Bench.run(module, "compress-1MB-level3") { Zstd.compress(data, 3) }
        Bench.run(module, "compress-1MB-level9", warmup = 3, iters = 15) {
            Zstd.compress(data, 9)
        }
        Bench.run(module, "decompress-1MB") { Zstd.decompress(compressed, data.size) }
        Bench.done(module)
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
