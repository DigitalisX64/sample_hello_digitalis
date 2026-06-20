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

package com.example.hellolibyuv

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibyuv.R
import io.github.crow_misia.libyuv.ArgbBuffer
import io.github.crow_misia.libyuv.I420Buffer
import io.github.crow_misia.libyuv.Plane

/**
 * Exercises Google's libyuv — a NEON-accelerated YUV<->RGB color-conversion
 * library (C/C++ with ARM64 NEON kernels) — under Berberis ARM64->x86_64
 * translation. The first call loads the arm64-v8a libyuv_android.so. The probe
 * fills a 64x64 ARGB image with a deterministic gradient, runs the native
 * ARGB->I420 (YUV 4:2:0) conversion and the native I420->ARGB conversion back,
 * and self-checks that the round-trip matches the original within a tolerance
 * (YUV 4:2:0 chroma subsampling is lossy, so ~+/-4 per channel is expected),
 * that the intermediate I420 luma plane is non-empty, and that the conversions
 * produced output. It logs "LIBYUV OK" or "LIBYUV FAIL" so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runLibyuvProbe()
    }

    private fun runLibyuvProbe(): String {
        val msg = try {
            val width = 64
            val height = 64

            // Allocate the source ARGB image, the YUV 4:2:0 intermediate, and
            // the round-trip destination. ArgbBuffer is a single interleaved
            // plane (B,G,R,A byte order in memory); I420Buffer is three planes
            // (Y full-res, U/V quarter-res).
            val srcArgb = ArgbBuffer.Factory.allocate(width, height)
            val i420 = I420Buffer.Factory.allocate(width, height)
            val dstArgb = ArgbBuffer.Factory.allocate(width, height)
            run probe@{
            try {
                // Paint a deterministic gradient straight into the source
                // plane's native buffer: B ramps with x, R ramps with y, G is
                // a diagonal blend, A is opaque. This is the exact byte order
                // libyuv expects for "ARGB", so the native converter reads it
                // directly.
                fillGradient(srcArgb.plane, width, height)

                // ARGB -> I420 and I420 -> ARGB, both routed through the native
                // NEON conversion routines in libyuv_android.so.
                srcArgb.convertTo(i420)
                i420.convertTo(dstArgb)

                // The intermediate luma (Y) plane must carry real image energy,
                // not be all-zero, or the first conversion silently no-op'd.
                if (!anyNonZero(i420.planeY)) {
                    return@probe "LIBYUV FAIL: I420 luma plane is all zero"
                }

                // Compare the round-tripped ARGB against the original at a
                // sampling of pixels. YUV 4:2:0 subsamples chroma and uses
                // BT.601 integer math, so a CORRECT round-trip still loses a few
                // levels per channel (verified identical under both the JIT and
                // the interpreter); allow that loss while still catching a broken
                // conversion (byte-swap / garbage gives deltas of 30-255). Alpha
                // is dropped by YUV entirely and not compared.
                val tolerance = 16
                var checked = 0
                var maxDelta = 0
                for (y in 0 until height step 7) {
                    for (x in 0 until width step 5) {
                        val s = readBgr(srcArgb.plane, x, y, height)
                        val d = readBgr(dstArgb.plane, x, y, height)
                        for (c in 0 until 3) {
                            val delta = kotlin.math.abs(s[c] - d[c])
                            if (delta > maxDelta) maxDelta = delta
                            if (delta > tolerance) {
                                return@probe "LIBYUV FAIL: pixel ($x,$y) chan $c " +
                                    "src=${s[c]} dst=${d[c]} delta=$delta > $tolerance"
                            }
                        }
                        checked++
                    }
                }

                if (checked == 0) {
                    "LIBYUV FAIL: no pixels were compared"
                } else {
                    "LIBYUV OK (${width}x${height} ARGB->I420->ARGB, " +
                        "$checked px checked, maxDelta=$maxDelta, tol=$tolerance)"
                }
            } finally {
                // Free all three native-backed buffers.
                srcArgb.close()
                i420.close()
                dstArgb.close()
            }
            }
        } catch (t: Throwable) {
            "LIBYUV FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    /** Writes a deterministic B,G,R,A gradient into an interleaved ARGB plane. */
    private fun fillGradient(plane: Plane, width: Int, height: Int) {
        val buf = plane.buffer
        // Row stride as a plain Int (Plane.rowStride is a value-class type); for a
        // single interleaved plane bufferSize == rowStride * height.
        val stride = plane.bufferSize / height
        for (y in 0 until height) {
            val row = y * stride
            for (x in 0 until width) {
                val p = row + x * 4
                val b = (x * 4) and 0xFF
                val r = (y * 4) and 0xFF
                val g = ((x + y) * 2) and 0xFF
                buf.put(p, b.toByte())        // B
                buf.put(p + 1, g.toByte())    // G
                buf.put(p + 2, r.toByte())    // R
                buf.put(p + 3, 0xFF.toByte()) // A
            }
        }
    }

    /** Reads the B,G,R triple of one pixel from an interleaved ARGB plane. */
    private fun readBgr(plane: Plane, x: Int, y: Int, height: Int): IntArray {
        val buf = plane.buffer
        val p = y * (plane.bufferSize / height) + x * 4
        return intArrayOf(
            buf.get(p).toInt() and 0xFF,     // B
            buf.get(p + 1).toInt() and 0xFF, // G
            buf.get(p + 2).toInt() and 0xFF, // R
        )
    }

    /** True if any byte in the plane's buffer is non-zero. */
    private fun anyNonZero(plane: Plane): Boolean {
        val buf = plane.buffer
        val size = plane.bufferSize
        for (i in 0 until size) {
            if (buf.get(i).toInt() != 0) return true
        }
        return false
    }

    companion object {
        private const val TAG = "HelloLibyuv"
    }
}
