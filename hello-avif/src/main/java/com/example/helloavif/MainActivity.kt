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
package com.example.helloavif

import android.graphics.Bitmap
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloavif.R
import org.aomedia.avif.android.AvifDecoder
import java.nio.ByteBuffer

/**
 * Exercises the AOMedia libavif Android decoder — libavif on top of the AV1
 * codec (dav1d/aom) — under Berberis ARM64->x86_64 translation. The first
 * AvifDecoder call loads the arm64-v8a libavif_android.so. The probe reads a
 * bundled real AVIF photo (assets/test.avif, 403x302) into a direct ByteBuffer,
 * calls AvifDecoder.getInfo() to parse the header, decodes the AV1 still image
 * into an ARGB_8888 Bitmap, and self-checks that the Bitmap dimensions match the
 * known image size and that a sampled pixel is opaque (non-zero alpha) — proving
 * the SIMD-heavy AV1 inverse-transform / motion-compensation / loop-filter inner
 * loops produced real pixels. Logs "AVIF OK" or "AVIF FAIL" so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Read the bundled AVIF asset into a *direct* ByteBuffer — the libavif
            // JNI requires a direct buffer it can hand straight to native code.
            val bytes = assets.open(ASSET_NAME).use { it.readBytes() }
            val encoded = ByteBuffer.allocateDirect(bytes.size).apply {
                put(bytes)
                rewind()
            }

            // Parse the AV1 image header.
            val info = AvifDecoder.Info()
            val gotInfo = AvifDecoder.getInfo(encoded, encoded.remaining(), info)
            when {
                !gotInfo ->
                    "AVIF FAIL: getInfo returned false"
                info.width != EXPECTED_WIDTH || info.height != EXPECTED_HEIGHT ->
                    "AVIF FAIL: header dims ${info.width}x${info.height}, " +
                        "expected ${EXPECTED_WIDTH}x$EXPECTED_HEIGHT"
                else -> {
                    // Decode the AV1 still image into an ARGB_8888 bitmap. The
                    // decoder reads the buffer from position 0, so rewind first.
                    encoded.rewind()
                    val bitmap = Bitmap.createBitmap(
                        info.width, info.height, Bitmap.Config.ARGB_8888
                    )
                    val decoded = AvifDecoder.decode(encoded, encoded.remaining(), bitmap)
                    if (!decoded) {
                        "AVIF FAIL: decode returned false"
                    } else {
                        val dimsOk =
                            bitmap.width == EXPECTED_WIDTH && bitmap.height == EXPECTED_HEIGHT
                        // A real photo decoded into an opaque-alpha bitmap: sample
                        // the centre pixel and confirm it is opaque (alpha == 0xFF).
                        // A blank/failed decode leaves zeroed pixels (alpha 0).
                        val centre = bitmap.getPixel(info.width / 2, info.height / 2)
                        val alpha = (centre ushr 24) and 0xFF
                        val pixelOk = alpha == 0xFF
                        if (dimsOk && pixelOk) {
                            "AVIF OK (${bitmap.width}x${bitmap.height}, depth=${info.depth}, " +
                                "centre=0x${Integer.toHexString(centre)})"
                        } else {
                            "AVIF FAIL: dimsOk=$dimsOk pixelOk=$pixelOk " +
                                "(${bitmap.width}x${bitmap.height}, centreAlpha=$alpha)"
                        }
                    }
                }
            }
        } catch (t: Throwable) {
            "AVIF FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloAvif"
        private const val ASSET_NAME = "test.avif"
        private const val EXPECTED_WIDTH = 403
        private const val EXPECTED_HEIGHT = 302
    }
}
