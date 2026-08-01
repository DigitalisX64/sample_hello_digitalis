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

package com.example.helloopenblas

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.helloopenblas.R
import org.bytedeco.javacpp.FloatPointer
import org.bytedeco.javacpp.Loader
import org.bytedeco.openblas.global.openblas
import kotlin.math.abs

/**
 * Exercises OpenBLAS — an optimized BLAS/LAPACK implementation, used here through
 * its Bytedeco/JavaCPP binding — under Berberis ARM64->x86_64 translation.
 * Loader.load() pulls in the arm64-v8a libopenblas.so + libjniopenblas.so; the
 * probe then drives OpenBLAS's single-precision floating-point matrix-multiply
 * kernel via cblas_sgemm (a 2x3 * 3x2 -> 2x2 product) and its dot-product kernel
 * via cblas_sdot, both over off-heap FloatPointer buffers. Every result element
 * is checked against the hand-computed answer within a 1e-4 tolerance. These
 * BLAS kernels are heavy NEON/SIMD floating-point paths, so a clean round-trip
 * confirms the translator's vector-float coverage. Logs "OPENBLAS OK" or
 * "OPENBLAS FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
        runBenchmarks()
    }

    /**
     * Dense single-precision matrix multiply — the workload the register
     * allocator is most exposed by. A 256x256x256 SGEMM is ~33M multiply-adds
     * over three resident matrices, so the inner kernel wants far more live
     * values than the 13 host registers a translated region can hold. Sized so
     * one iteration is milliseconds rather than microseconds: below about a
     * millisecond, scheduling jitter on the emulator swamps the measurement.
     */
    private fun runBenchmarks() {
        val module = "hello-openblas"
        val n = 256
        val a = FloatPointer((n * n).toLong())
        val b = FloatPointer((n * n).toLong())
        val c = FloatPointer((n * n).toLong())
        try {
            for (i in 0 until n * n) {
                a.put(i.toLong(), ((i % 17) + 1).toFloat())
                b.put(i.toLong(), ((i % 13) + 1).toFloat())
            }
            Bench.run(module, "sgemm-256", warmup = 5, iters = 15) {
                openblas.cblas_sgemm(
                    openblas.CblasRowMajor, openblas.CblasNoTrans, openblas.CblasNoTrans,
                    n, n, n, 1f, a, n, b, n, 0f, c, n,
                )
            }
            Bench.run(module, "saxpy-65k-x200", warmup = 5, iters = 20) {
                repeat(200) { openblas.cblas_saxpy(n * n, 2.0f, a, 1, c, 1) }
            }
        } finally {
            a.deallocate(); b.deallocate(); c.deallocate()
        }
        Bench.done(module)
    }

    private fun runProbe(): String {
        var a: FloatPointer? = null
        var b: FloatPointer? = null
        var c: FloatPointer? = null
        var x: FloatPointer? = null
        var y: FloatPointer? = null
        val msg = try {
            // Force the OpenBLAS native libraries (libopenblas.so via
            // libjniopenblas.so) to load. This pulls in the bionic-linked
            // arm64-v8a natives and returns the absolute path of the JNI .so it
            // loaded. Pass the openblas global bindings class so JavaCPP resolves
            // the right library name.
            val loaded = Loader.load(openblas::class.java)

            // --- cblas_sgemm: single-precision matrix multiply C = A * B.
            //   A is 2x3 (M=2, K=3), B is 3x2 (K=3, N=2), C is 2x2 (M=2, N=2).
            //   Row-major storage; alpha=1, beta=0, no transpose.
            //   A = [ 1 2 3 ]      B = [  7  8 ]      C = A*B = [  58  64 ]
            //       [ 4 5 6 ]          [  9 10 ]                [ 139 154 ]
            //                          [ 11 12 ]
            val m = 2
            val n = 2
            val k = 3
            a = FloatPointer(1f, 2f, 3f, 4f, 5f, 6f)
            b = FloatPointer(7f, 8f, 9f, 10f, 11f, 12f)
            c = FloatPointer(0f, 0f, 0f, 0f)
            openblas.cblas_sgemm(
                openblas.CblasRowMajor, openblas.CblasNoTrans, openblas.CblasNoTrans,
                m, n, k,
                1.0f, a, k,   // A, lda = K (row-major: A's column count)
                b, n,         // B, ldb = N (row-major: B's column count)
                0.0f, c, n    // C, ldc = N (row-major: C's column count)
            )
            val expectedC = floatArrayOf(58f, 64f, 139f, 154f)
            var gemmOk = true
            val gotC = FloatArray(4)
            for (idx in 0 until 4) {
                val v = c.get(idx.toLong())
                gotC[idx] = v
                if (abs(v - expectedC[idx]) > 1e-4f) gemmOk = false
            }

            // --- cblas_sdot: single-precision dot product of two length-5 vectors.
            //   x . y = 1*2 + 2*4 + 3*6 + 4*8 + 5*10 = 2+8+18+32+50 = 110
            val len = 5
            x = FloatPointer(1f, 2f, 3f, 4f, 5f)
            y = FloatPointer(2f, 4f, 6f, 8f, 10f)
            val dot = openblas.cblas_sdot(len, x, 1, y, 1)
            val expectedDot = 110.0f
            val dotOk = abs(dot - expectedDot) <= 1e-4f

            if (gemmOk && dotOk) {
                "OPENBLAS OK (loaded ${shortName(loaded)}; " +
                    "sgemm 2x3*3x2 -> C=${gotC.joinToString(",") { fmt(it) }}; " +
                    "sdot len$len -> ${fmt(dot)})"
            } else {
                "OPENBLAS FAIL: gemmOk=$gemmOk dotOk=$dotOk " +
                    "C=${gotC.joinToString(",") { fmt(it) }} dot=${fmt(dot)}"
            }
        } catch (t: Throwable) {
            "OPENBLAS FAIL: ${t.javaClass.simpleName}: ${t.message}"
        } finally {
            // Free the off-heap native buffers.
            a?.deallocate()
            b?.deallocate()
            c?.deallocate()
            x?.deallocate()
            y?.deallocate()
        }
        Log.i(TAG, msg)
        return msg
    }

    private fun fmt(v: Float): String = String.format("%.1f", v)

    private fun shortName(path: String?): String =
        path?.substringAfterLast('/') ?: "<null>"

    companion object {
        private const val TAG = "HelloOpenBLAS"
    }
}
