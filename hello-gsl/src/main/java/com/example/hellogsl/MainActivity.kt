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

package com.example.hellogsl

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellogsl.R
import org.bytedeco.gsl.global.gsl
import org.bytedeco.javacpp.Loader
import kotlin.math.abs

/**
 * Exercises the GNU Scientific Library (GSL 2.8) via its Bytedeco/JavaCPP
 * binding under Berberis ARM64->x86_64 translation. Loader.load() pulls in the
 * arm64-v8a libgsl.so + libjnigsl.so; the probe then evaluates several of GSL's
 * double-precision special functions (Bessel J0, the gamma function, and the
 * error function) at known arguments and asserts each result is within 1e-9 of
 * its mathematically-exact reference value.
 *
 * These functions are pure transcendental / special-function math implemented
 * with rational Chebyshev approximations, polynomial evaluation, and libm calls
 * (exp/log/sqrt/sin) — a dense double-precision FP and integer-control workload
 * that drives the translator's floating-point path hard. The probe logs
 * "GSL OK (...)" or "GSL FAIL: ..." so the suite's StatusTest can assert a clean
 * run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Force the GSL native libraries (libgsl.so + the libjnigsl.so JNI
            // peer) to load. This is the first call into the bionic-linked
            // arm64-v8a natives; it returns the absolute path of the .so it
            // loaded. Pass the generated global class explicitly so JavaCPP
            // resolves the correct library name.
            val loaded = Loader.load(gsl::class.java)

            // Each case: (label, arg, computed, reference). Tolerance 1e-9.
            val cases = listOf(
                Case("gsl_sf_bessel_J0(1.0)", 1.0,
                    gsl.gsl_sf_bessel_J0(1.0), 0.7651976865579666),
                Case("gsl_sf_bessel_J0(5.0)", 5.0,
                    gsl.gsl_sf_bessel_J0(5.0), -0.1775967713143383),
                Case("gsl_sf_gamma(5.0)", 5.0,
                    gsl.gsl_sf_gamma(5.0), 24.0),
                Case("gsl_sf_erf(1.0)", 1.0,
                    gsl.gsl_sf_erf(1.0), 0.8427007929497149),
            )

            val tol = 1e-9
            val failures = cases.filter { abs(it.computed - it.reference) > tol }

            if (failures.isEmpty()) {
                val summary = cases.joinToString("; ") { "${it.label}=${it.computed}" }
                "GSL OK (loaded ${shortName(loaded)}; $summary)"
            } else {
                val detail = failures.joinToString("; ") {
                    "${it.label} got ${it.computed} expected ${it.reference} " +
                        "(|d|=${abs(it.computed - it.reference)})"
                }
                "GSL FAIL: $detail"
            }
        } catch (t: Throwable) {
            "GSL FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    private data class Case(
        val label: String,
        val arg: Double,
        val computed: Double,
        val reference: Double,
    )

    private fun shortName(path: String?): String =
        path?.substringAfterLast('/') ?: "<null>"

    companion object {
        private const val TAG = "HelloGsl"
    }
}
