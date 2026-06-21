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
package com.example.hellolibwebp

import android.graphics.Bitmap
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.aureusapps.android.webpandroid.decoder.WebPDecoder
import com.aureusapps.android.webpandroid.encoder.WebPConfig
import com.aureusapps.android.webpandroid.encoder.WebPEncoder
import com.aureusapps.android.webpandroid.encoder.WebPPreset
import com.example.hellodigitalis.hellolibwebp.R
import java.io.File
import java.nio.ByteBuffer
import kotlin.math.abs

/**
 * Exercises Google's libwebp (the WebP image codec, native C) under Berberis
 * ARM64->x86_64 translation, via the aureusapps webp-android JNI bridge whose
 * AAR ships jni/arm64-v8a/libwebp.so. The probe:
 *
 *   1. Synthesizes a 96x96 ARGB_8888 Bitmap with four solid colored quadrants.
 *   2. Lossless-encodes it to a .webp file in cacheDir (WebPEncoder.encode),
 *      driving libwebp's VP8L lossless encoder.
 *   3. Re-reads the file bytes and confirms the RIFF/WEBP container magic.
 *   4. Decodes those bytes back to a Bitmap (WebPDecoder over a direct
 *      ByteBuffer), driving libwebp's VP8L Huffman + predictor/color-transform
 *      decode loops.
 *   5. Self-checks that the decoded image has the same dimensions and that its
 *      pixels match the source near-exactly (lossless tolerance).
 *
 * Logs "WEBP OK ..." on success or "WEBP FAIL: ..." on any failure so the
 * suite's StatusTest can assert a clean run. Everything is wrapped in a single
 * try/catch(Throwable); the success line never contains a crash/FAIL marker.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runWebpProbe()
    }

    private fun runWebpProbe(): String {
        val msg = try {
            val width = 96
            val height = 96
            val source = makeQuadrantBitmap(width, height)

            // Capture the four quadrant probe pixels NOW: WebPEncoder.encode()
            // recycles the source bitmap, so it cannot be read after encoding.
            val probePoints = listOf(
                width / 4 to height / 4,
                3 * width / 4 to height / 4,
                width / 4 to 3 * height / 4,
                3 * width / 4 to 3 * height / 4
            )
            val expectedColors = probePoints.map { (x, y) -> source.getPixel(x, y) }

            // --- Encode: Bitmap -> lossless .webp file in cacheDir. ---
            val webpFile = File(cacheDir, "hello-libwebp-roundtrip.webp")
            if (webpFile.exists()) webpFile.delete()

            val encoder = WebPEncoder(this, width, height)
            try {
                encoder.configure(
                    // lossless = 1 lets us require a near-exact pixel match on
                    // the round-trip; quality 100 maximizes lossless effort.
                    config = WebPConfig(
                        lossless = WebPConfig.COMPRESSION_LOSSLESS,
                        quality = 100f
                    ),
                    preset = WebPPreset.WEBP_PRESET_DEFAULT
                )
                encoder.encode(source, Uri.fromFile(webpFile))
            } finally {
                encoder.release()
            }

            val webpBytes = webpFile.readBytes()

            // --- Verify the RIFF/WEBP container magic. ---
            // Bytes [0..3] = "RIFF", [8..11] = "WEBP".
            val hasRiff = webpBytes.size >= 12 &&
                webpBytes[0] == 'R'.code.toByte() &&
                webpBytes[1] == 'I'.code.toByte() &&
                webpBytes[2] == 'F'.code.toByte() &&
                webpBytes[3] == 'F'.code.toByte() &&
                webpBytes[8] == 'W'.code.toByte() &&
                webpBytes[9] == 'E'.code.toByte() &&
                webpBytes[10] == 'B'.code.toByte() &&
                webpBytes[11] == 'P'.code.toByte()
            if (!hasRiff) {
                return@runWebpProbe fail(
                    "encoded stream missing RIFF/WEBP magic (${webpBytes.size} bytes)"
                )
            }

            // --- Decode: .webp bytes -> Bitmap. ---
            // nativeSetDataBuffer reads through JNI's direct-buffer address, so
            // the data must live in a DIRECT ByteBuffer.
            val directBuf = ByteBuffer.allocateDirect(webpBytes.size)
            directBuf.put(webpBytes)
            directBuf.rewind()

            val decoder = WebPDecoder(this)
            val decoded: Bitmap?
            val info: com.aureusapps.android.webpandroid.decoder.WebPInfo
            try {
                decoder.setDataBuffer(directBuf)
                info = decoder.decodeInfo()
                // decoder.release() (in finally) recycles the frame bitmap, so
                // take an independent copy to read pixels from afterward.
                decoded = decoder.decodeNextFrame().frame?.copy(Bitmap.Config.ARGB_8888, false)
            } finally {
                decoder.release()
            }
            if (decoded == null) {
                return@runWebpProbe fail("decodeNextFrame returned a null frame")
            }

            // --- Self-check dimensions. ---
            if (info.width != width || info.height != height) {
                return@runWebpProbe fail(
                    "decodeInfo dimensions ${info.width}x${info.height} != ${width}x$height"
                )
            }
            if (decoded.width != width || decoded.height != height) {
                return@runWebpProbe fail(
                    "decoded bitmap ${decoded.width}x${decoded.height} != ${width}x$height"
                )
            }

            // --- Self-check pixels (lossless => near-exact). ---
            // Sample one pixel in each quadrant; allow a tiny per-channel
            // tolerance to absorb any RGBA<->ARGB rounding in the bridge.
            val tolerance = 4
            for (i in probePoints.indices) {
                val (x, y) = probePoints[i]
                val expected = expectedColors[i]
                val actual = decoded.getPixel(x, y)
                if (!pixelsClose(expected, actual, tolerance)) {
                    return@runWebpProbe fail(
                        "pixel ($x,$y) expected #${Integer.toHexString(expected)} " +
                            "got #${Integer.toHexString(actual)}"
                    )
                }
            }

            "WEBP OK (${width}x$height encode->decode round-trip, " +
                "${webpBytes.size} webp bytes)"
        } catch (t: Throwable) {
            fail("${t.javaClass.simpleName}: ${t.message}")
        }
        Log.i(TAG, msg)
        return msg
    }

    /** Builds a recognizable Bitmap: four solid colored quadrants. */
    private fun makeQuadrantBitmap(width: Int, height: Int): Bitmap {
        val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        val halfW = width / 2
        val halfH = height / 2
        for (y in 0 until height) {
            for (x in 0 until width) {
                val color = when {
                    x < halfW && y < halfH -> Color.rgb(220, 30, 30)   // top-left red
                    x >= halfW && y < halfH -> Color.rgb(30, 200, 30)  // top-right green
                    x < halfW && y >= halfH -> Color.rgb(30, 60, 220)  // bottom-left blue
                    else -> Color.rgb(230, 220, 40)                    // bottom-right yellow
                }
                bitmap.setPixel(x, y, color)
            }
        }
        return bitmap
    }

    private fun pixelsClose(expected: Int, actual: Int, tolerance: Int): Boolean {
        return abs(Color.red(expected) - Color.red(actual)) <= tolerance &&
            abs(Color.green(expected) - Color.green(actual)) <= tolerance &&
            abs(Color.blue(expected) - Color.blue(actual)) <= tolerance
    }

    private fun fail(reason: String): String = "WEBP FAIL: $reason"

    companion object {
        private const val TAG = "HelloLibwebp"
    }
}
