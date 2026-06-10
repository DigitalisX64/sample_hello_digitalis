package com.example.hellorenderscripttoolkit

import android.graphics.Bitmap
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellorenderscripttoolkit.R
import com.google.android.renderscript.Toolkit

/**
 * Exercises Google's RenderScript Intrinsics Replacement Toolkit — native (C++)
 * image ops that use Neon/AdvSimd on Arm — under Berberis ARM64->x86_64
 * translation. Loading the Toolkit pulls in the arm64-v8a
 * librenderscript-toolkit.so; the probe runs two deterministic image ops on a
 * solid-color bitmap and self-checks the pixels, logging "RSTOOLKIT OK" or
 * "RSTOOLKIT FAIL" so the suite's StatusTest can assert a clean run.
 *
 * The checks are deterministic by construction:
 *   - blur of a constant image is the same constant (a box/Gaussian blur of a
 *     flat field reproduces the field), so every output pixel must equal the
 *     input color (allowing +/-1 per channel for rounding).
 *   - histogram of a solid NxN image must tally exactly N*N counts into the one
 *     bucket each channel's byte lands in, and zero elsewhere.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runToolkitProbe()
    }

    private fun runToolkitProbe(): String {
        return try {
            val size = 64
            val a = 0xFF; val r = 0x80; val g = 0x40; val b = 0x20
            val color = Color.argb(a, r, g, b)

            val input = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
            input.eraseColor(color)

            val failed = mutableListOf<String>()

            // --- Probe 1: blur of a solid image stays solid. ---
            val blurred = Toolkit.blur(input, radius = 5)
            if (blurred.width != size || blurred.height != size) {
                failed.add("blur-dims(${blurred.width}x${blurred.height})")
            } else {
                val center = blurred.getPixel(size / 2, size / 2)
                if (!pixelsClose(center, color, tolerance = 1)) {
                    failed.add("blur-center(${hex(center)}!=${hex(color)})")
                }
                // Spot-check a corner too — blur near edges clamps, but on a flat
                // field it still reproduces the constant.
                val corner = blurred.getPixel(0, 0)
                if (!pixelsClose(corner, color, tolerance = 1)) {
                    failed.add("blur-corner(${hex(corner)}!=${hex(color)})")
                }
            }

            // --- Probe 2: histogram of a solid image is a set of single spikes. ---
            // ARGB_8888 -> vectorSize 4. The toolkit lays the 256*4 ints out
            // INTERLEAVED by value: "the counts for value 0 are consecutive,
            // followed by those for value 1, etc." i.e. index = value*4 + channel
            // with channel order R,G,B,A (see Histogram.cpp: sums[(in[c]<<2)+c]).
            val hist = Toolkit.histogram(input)
            val pixels = size * size
            if (hist.size != 256 * 4) {
                failed.add("hist-size(${hist.size})")
            } else {
                val channels = listOf("R" to r, "G" to g, "B" to b, "A" to a)
                channels.forEachIndexed { ch, (name, value) ->
                    val total = (0 until 256).sumOf { hist[it * 4 + ch].toLong() }
                    if (total != pixels.toLong()) {
                        failed.add("hist-$name-total($total!=$pixels)")
                    } else if (hist[value * 4 + ch] != pixels) {
                        failed.add("hist-$name-spike(${hist[value * 4 + ch]}@$value!=$pixels)")
                    }
                }
            }

            val msg = if (failed.isEmpty()) {
                "RSTOOLKIT OK (blur+histogram on ${size}x$size solid #${hex(color)})"
            } else {
                "RSTOOLKIT FAIL: ${failed.joinToString(",")}"
            }
            Log.i(TAG, msg)
            msg
        } catch (t: Throwable) {
            val msg = "RSTOOLKIT FAIL: ${t.javaClass.simpleName}: ${t.message}"
            Log.e(TAG, msg, t)
            msg
        }
    }

    private fun pixelsClose(p: Int, q: Int, tolerance: Int): Boolean {
        return Math.abs(Color.alpha(p) - Color.alpha(q)) <= tolerance &&
            Math.abs(Color.red(p) - Color.red(q)) <= tolerance &&
            Math.abs(Color.green(p) - Color.green(q)) <= tolerance &&
            Math.abs(Color.blue(p) - Color.blue(q)) <= tolerance
    }

    private fun hex(c: Int): String = String.format("%08X", c)

    companion object {
        private const val TAG = "HelloRSToolkit"
    }
}
