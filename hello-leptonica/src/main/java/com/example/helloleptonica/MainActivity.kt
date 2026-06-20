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

package com.example.helloleptonica

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloleptonica.R
import org.bytedeco.javacpp.IntPointer
import org.bytedeco.javacpp.Loader
import org.bytedeco.leptonica.PIX
import org.bytedeco.leptonica.global.leptonica.pixCreate
import org.bytedeco.leptonica.global.leptonica.pixDestroy
import org.bytedeco.leptonica.global.leptonica.pixGetHeight
import org.bytedeco.leptonica.global.leptonica.pixGetPixel
import org.bytedeco.leptonica.global.leptonica.pixGetWidth
import org.bytedeco.leptonica.global.leptonica.pixScale
import org.bytedeco.leptonica.global.leptonica.pixSetPixel

/**
 * Exercises Leptonica — the C image-processing library that Tesseract is built
 * on, wrapped by Bytedeco/JavaCPP — under Berberis ARM64->x86_64 translation.
 * Loader.load() pulls in the arm64-v8a libjnileptonica.so (and libleptonica);
 * the probe then drives real native image processing: it pixCreate()s an 8-bpp
 * grayscale PIX, writes a deterministic per-pixel pattern through the native
 * pixSetPixel() accessor, reads several pixels back through native pixGetPixel()
 * (whose result is returned via an output IntPointer) and asserts they match,
 * then runs a genuine native resampling op — pixScale(2x, 2x) — and asserts the
 * scaled PIX is non-null with the expected doubled dimensions. Both PIX buffers
 * are released with pixDestroy(). This drives Leptonica's native allocation,
 * raster read/write, and bilinear-scale code paths, logging "LEPTONICA OK" or
 * "LEPTONICA FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Force the Leptonica native libraries (libjnileptonica.so +
            // libleptonica.so) to load. This is the first call into the
            // bionic-linked arm64-v8a native; it returns the absolute path of
            // the loaded .so. Pass the leptonica global class explicitly so the
            // loader resolves the correct library name.
            val loaded = Loader.load(org.bytedeco.leptonica.global.leptonica::class.java)

            val width = 32
            val height = 16
            val depth = 8

            // --- Create an 8-bpp grayscale PIX (32x16) natively.
            val pix: PIX = pixCreate(width, height, depth)
                ?: throw IllegalStateException("pixCreate returned null")

            var ok = pixGetWidth(pix) == width &&
                pixGetHeight(pix) == height

            // Write a deterministic 0..255 pattern through the native accessor.
            fun patternAt(x: Int, y: Int): Int = ((x * 7 + y * 13) and 0xFF)
            var y = 0
            while (y < height) {
                var x = 0
                while (x < width) {
                    pixSetPixel(pix, x, y, patternAt(x, y))
                    x++
                }
                y++
            }

            // Read several pixels back through native pixGetPixel(). The pixel
            // value comes out via the output IntPointer, not the int return
            // (which is a 0/1 error code).
            val out = IntPointer(1L)
            val samples = arrayOf(
                intArrayOf(0, 0),
                intArrayOf(31, 15),
                intArrayOf(7, 3),
                intArrayOf(17, 11),
                intArrayOf(30, 0),
                intArrayOf(0, 15)
            )
            for (s in samples) {
                val rc = pixGetPixel(pix, s[0], s[1], out)
                val got = out.get(0)
                val expected = patternAt(s[0], s[1])
                if (rc != 0 || got != expected) ok = false
            }
            out.deallocate()

            // --- Run a real native resampling op: scale 2x in both axes.
            val scaled: PIX = pixScale(pix, 2.0f, 2.0f)
                ?: throw IllegalStateException("pixScale returned null")
            val sw = pixGetWidth(scaled)
            val sh = pixGetHeight(scaled)
            val scaleOk = sw == width * 2 && sh == height * 2

            pixDestroy(scaled)
            pixDestroy(pix)

            if (ok && scaleOk) {
                "LEPTONICA OK (loaded ${shortName(loaded)}; " +
                    "PIX ${width}x$height@${depth}bpp pattern round-tripped; " +
                    "pixScale -> ${sw}x$sh)"
            } else {
                "LEPTONICA FAIL: pixelOk=$ok scaleOk=$scaleOk " +
                    "scaled=${sw}x$sh (expected ${width * 2}x${height * 2})"
            }
        } catch (t: Throwable) {
            "LEPTONICA FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    private fun shortName(path: String?): String =
        path?.substringAfterLast('/') ?: "<null>"

    companion object {
        private const val TAG = "HelloLeptonica"
    }
}
