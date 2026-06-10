package com.example.hellogpuimage

import android.graphics.Bitmap
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellogpuimage.R
import jp.co.cyberagent.android.gpuimage.GPUImage
import jp.co.cyberagent.android.gpuimage.filter.GPUImageColorInvertFilter
import kotlin.math.abs

/**
 * Exercises GPUImage — GPU-accelerated image filtering via OpenGL ES shaders,
 * with a native (C) libyuv decoder — under Berberis ARM64->x86_64 translation.
 * The arm64-v8a libyuv-decoder.so loads on first use; the GLES filter pass runs
 * on the emulator GPU through ANGLE.
 *
 * The probe builds a solid-red 8x8 bitmap, applies a GPUImageColorInvertFilter
 * through GPUImage's synchronous offscreen path (getBitmapWithFilterApplied,
 * which spins up its own EGL pbuffer via PixelBuffer — no Activity surface
 * needed), and self-checks that a center pixel inverted from red (0xFFFF0000) to
 * cyan (0xFF00FFFF) within tolerance, logging "GPUIMAGE OK" or "GPUIMAGE FAIL"
 * so the suite's StatusTest can assert a clean run. Filtering touches the GL
 * context, so it runs on a background thread; the result lands on sample_text
 * via the UI thread.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        Thread {
            val msg = runGpuImageProbe()
            runOnUiThread {
                findViewById<TextView>(R.id.sample_text).text = msg
            }
        }.start()
    }

    private fun runGpuImageProbe(): String {
        val msg = try {
            // A small solid-red source. GPUImageColorInvertFilter computes
            // (1 - rgb), so red (255,0,0) must invert to cyan (0,255,255).
            val size = 8
            val input = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888)
            input.eraseColor(Color.RED) // 0xFFFF0000

            val gpuImage = GPUImage(this)
            gpuImage.setFilter(GPUImageColorInvertFilter())
            val output = gpuImage.getBitmapWithFilterApplied(input)

            when {
                output == null ->
                    "GPUIMAGE FAIL: getBitmapWithFilterApplied returned null"
                output.width != size || output.height != size ->
                    "GPUIMAGE FAIL: size ${output.width}x${output.height}, " +
                        "expected ${size}x$size"
                else -> {
                    val px = output.getPixel(size / 2, size / 2)
                    val r = Color.red(px)
                    val g = Color.green(px)
                    val b = Color.blue(px)
                    // Expect cyan: R near 0, G and B near 255. Allow a small
                    // tolerance for GPU/ANGLE rounding in the shader path.
                    val tol = 8
                    val inverted = abs(r - 0) <= tol &&
                        abs(g - 255) <= tol &&
                        abs(b - 255) <= tol
                    val hex = String.format("#%08X", px)
                    if (inverted) {
                        "GPUIMAGE OK (red->cyan, center=$hex, rgb=$r,$g,$b)"
                    } else {
                        "GPUIMAGE FAIL: center=$hex rgb=$r,$g,$b, " +
                            "expected ~0,255,255"
                    }
                }
            }
        } catch (t: Throwable) {
            "GPUIMAGE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloGPUImage"
    }
}
