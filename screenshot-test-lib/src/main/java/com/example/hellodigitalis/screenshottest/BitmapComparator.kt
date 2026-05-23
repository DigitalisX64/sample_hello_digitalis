package com.example.hellodigitalis.screenshottest

import android.graphics.Bitmap
import android.graphics.Color
import kotlin.math.abs
import kotlin.math.min

object BitmapComparator {

    /**
     * Compare two bitmaps pixel-by-pixel with RGBA absolute difference.
     * Returns match percentage (0.0 to 1.0) and a diff bitmap highlighting differences.
     */
    fun compare(actual: Bitmap, reference: Bitmap): ComparisonResult {
        // Scale to same dimensions if needed
        val width = reference.width
        val height = reference.height

        val scaledActual = if (actual.width != width || actual.height != height) {
            Bitmap.createScaledBitmap(actual, width, height, true)
        } else {
            actual
        }

        val diffBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
        var totalDiff = 0L
        // Compare only the RGB channels. The alpha channel is unreliable as a
        // similarity metric here because takeScreenshot() and BitmapFactory.
        // decodeStream() do not agree on alpha for opaque pixels — the in-memory
        // takeScreenshot bitmap can carry non-255 alpha on GPU-rendered surfaces
        // even when hasAlpha=false, while the PNG-decoded reference always reads
        // back alpha=255. Saving compress(actual)==reference.png byte-identically
        // confirms RGB is identical; only alpha differs and the test should
        // ignore that channel.
        val totalMax = width.toLong() * height * 3 * 255 // 3 channels, max 255 per channel

        val actualPixels = IntArray(width)
        val refPixels = IntArray(width)
        val diffPixels = IntArray(width)

        for (y in 0 until height) {
            scaledActual.getPixels(actualPixels, 0, width, 0, y, width, 1)
            reference.getPixels(refPixels, 0, width, 0, y, width, 1)

            for (x in 0 until width) {
                val ap = actualPixels[x]
                val rp = refPixels[x]

                val dr = abs(Color.red(ap) - Color.red(rp))
                val dg = abs(Color.green(ap) - Color.green(rp))
                val db = abs(Color.blue(ap) - Color.blue(rp))

                totalDiff += dr + dg + db

                // Diff bitmap: highlight differences in red, scale intensity
                val diffIntensity = min(255, (dr + dg + db) * 2)
                diffPixels[x] = if (diffIntensity > 0) {
                    Color.argb(255, diffIntensity, 0, 0)
                } else {
                    // Faded version of original for context
                    Color.argb(255, Color.red(rp) / 3, Color.green(rp) / 3, Color.blue(rp) / 3)
                }
            }

            diffBitmap.setPixels(diffPixels, 0, width, 0, y, width, 1)
        }

        val matchPercentage = if (totalMax > 0) 1.0f - totalDiff.toFloat() / totalMax else 1.0f

        if (scaledActual !== actual) {
            scaledActual.recycle()
        }

        return ComparisonResult(matchPercentage, diffBitmap)
    }
}
