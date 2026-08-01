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

package com.example.hellofftw

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.hellofftw.R
import org.bytedeco.fftw.global.fftw3
import org.bytedeco.javacpp.DoublePointer
import org.bytedeco.javacpp.Loader
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.hypot

/**
 * Exercises FFTW (the Fastest Fourier Transform in the West) via the
 * Bytedeco/JavaCPP binding under Berberis ARM64->x86_64 translation. This
 * drives FFTW's complex floating-point butterfly kernels — the core radix-N
 * DFT arithmetic that stresses double-precision NEON/SIMD multiply-add through
 * the translator.
 *
 * The probe loads libfftw3.so via Loader.load(fftw3), builds an N=16 complex
 * input that is a pure cosine at bin k=2 (re = cos(2*pi*2*n/N), im = 0) in an
 * interleaved [re, im] DoublePointer, plans a forward 1D complex-to-complex
 * DFT (fftw_plan_dft_1d with FFTW_FORWARD, FFTW_ESTIMATE), executes it, and
 * checks the magnitude spectrum: a real cosine at bin 2 must produce two clear
 * conjugate-symmetric peaks at bins 2 and N-2 (=14) and near-zero everywhere
 * else. Logs "FFTW OK (...)" on success or "FFTW FAIL: ..." otherwise, so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
        runBenchmarks()
    }

    private fun runProbe(): String {
        val msg = try {
            // Force the FFTW native libraries (libfftw3.so + libjnifftw.so) to
            // load. This is the first call into the bionic-linked arm64-v8a
            // native and returns the absolute path of the .so it loaded.
            val loaded = Loader.load(fftw3::class.java)

            val n = 16
            val k = 2                 // input frequency bin (a pure cosine)
            val complexLen = 2L * n   // interleaved re,im DoublePointer length

            // Off-heap complex in/out buffers. FFTW lays each complex sample out
            // as two adjacent doubles [re, im]; index 2*j is re, 2*j+1 is im.
            val input = DoublePointer(complexLen)
            val output = DoublePointer(complexLen)

            // Build a pure real cosine at bin k=2: re = cos(2*pi*k*n/N), im = 0.
            for (nn in 0 until n) {
                input.put(2L * nn, cos(2.0 * PI * k * nn / n))
                input.put(2L * nn + 1, 0.0)
            }

            // Plan + execute a forward 1D complex-to-complex DFT. The plan
            // captures the in/out buffer addresses; fftw_execute runs the FFT.
            val plan = fftw3.fftw_plan_dft_1d(
                n, input, output, FFTW_FORWARD, FFTW_ESTIMATE
            )
            if (plan == null || plan.isNull) {
                throw IllegalStateException("fftw_plan_dft_1d returned null plan")
            }
            fftw3.fftw_execute(plan)

            // Magnitude spectrum from the interleaved output buffer.
            val mag = DoubleArray(n)
            for (j in 0 until n) {
                val re = output.get(2L * j)
                val im = output.get(2L * j + 1)
                mag[j] = hypot(re, im)
            }

            // Tear down: destroy the plan and free the off-heap buffers.
            fftw3.fftw_destroy_plan(plan)
            input.deallocate()
            output.deallocate()

            // A real cosine at bin k splits into conjugate-symmetric peaks at
            // bins k and N-k, each of magnitude N/2. Verify the two peaks
            // dominate and every other bin is near zero relative to the peak.
            val peakLo = mag[k]
            val peakHi = mag[n - k]
            val expectedPeak = n / 2.0       // = 8.0 for N=16

            var maxOther = 0.0
            for (j in 0 until n) {
                if (j == k || j == n - k) continue
                if (mag[j] > maxOther) maxOther = mag[j]
            }

            val peaksOk =
                kotlin.math.abs(peakLo - expectedPeak) < 1e-6 &&
                    kotlin.math.abs(peakHi - expectedPeak) < 1e-6
            // "near-zero elsewhere": every non-peak bin must be a tiny fraction
            // of the peak. Numerical FFT noise lands far below 1% of N/2.
            val epsilon = expectedPeak * 1e-9
            val othersOk = maxOther < epsilon

            if (peaksOk && othersOk) {
                "FFTW OK (loaded ${shortName(loaded)}; N=$n forward DFT of " +
                    "cos@bin$k -> peaks ${fmt(peakLo)}@$k & ${fmt(peakHi)}@" +
                    "${n - k} (expect ${fmt(expectedPeak)}); " +
                    "max other bin ${fmt(maxOther)} < ${fmt(epsilon)})"
            } else {
                "FFTW FAIL: peaksOk=$peaksOk othersOk=$othersOk " +
                    "peak[$k]=${fmt(peakLo)} peak[${n - k}]=${fmt(peakHi)} " +
                    "expect=${fmt(expectedPeak)} maxOther=${fmt(maxOther)} " +
                    "eps=${fmt(epsilon)}"
            }
        } catch (t: Throwable) {
            "FFTW FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    private fun shortName(path: String?): String =
        path?.substringAfterLast('/') ?: "<null>"

    private fun fmt(v: Double): String = String.format("%.4g", v)

    /**
     * A 64K-point complex FFT: double-precision butterflies over a working set
     * far larger than cache, so it mixes heavy scalar FP with a strided memory
     * access pattern. The plan is built once, outside the timed loop — planning
     * is a measurement of FFTW's search, not of translation.
     */
    private fun runBenchmarks() {
        val module = "hello-fftw"
        val n = 1 shl 16
        val complexLen = 2L * n
        val input = DoublePointer(complexLen)
        val output = DoublePointer(complexLen)
        try {
            for (i in 0 until n) {
                input.put(2L * i, cos(2.0 * PI * 4.0 * i / n))
                input.put(2L * i + 1, 0.0)
            }
            val plan = fftw3.fftw_plan_dft_1d(n, input, output, fftw3.FFTW_FORWARD, fftw3.FFTW_ESTIMATE)
            try {
                Bench.run(module, "fft-64k-complex", warmup = 5, iters = 20) {
                    fftw3.fftw_execute(plan)
                }
            } finally {
                fftw3.fftw_destroy_plan(plan)
            }
        } finally {
            input.deallocate(); output.deallocate()
        }
        Bench.done(module)
    }

    companion object {
        private const val TAG = "HelloFftw"

        // fftw3.h sign/flag constants (not exposed as named fields by the
        // binding): FFTW_FORWARD = -1, FFTW_ESTIMATE = 1 << 6.
        private const val FFTW_FORWARD = -1
        private const val FFTW_ESTIMATE = 1 shl 6   // = 64
    }
}
