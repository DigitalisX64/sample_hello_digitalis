package com.example.hellogif

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellogif.R
import pl.droidsonroids.gif.GifDrawable

/**
 * Exercises android-gif-drawable — its arm64-v8a libpl_droidsonroids_gif.so is a
 * native GIF decoder — under Berberis ARM64->x86_64 translation. The probe loads
 * a bundled 5-frame 64x64 GIF, asserts its frame count and dimensions, and
 * force-decodes a frame to a Bitmap, logging "GIF OK" / "GIF FAIL" for the
 * suite's StatusTest.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        return try {
            val gif = GifDrawable(assets, "test.gif")
            val frames = gif.numberOfFrames
            val w = gif.intrinsicWidth
            val h = gif.intrinsicHeight
            val duration = gif.duration

            // Force native decode of a specific frame into a Bitmap.
            val frameBmp = gif.seekToFrameAndGet(2)
            val decodedOk = frameBmp != null && frameBmp.width == 64 && frameBmp.height == 64
            frameBmp?.recycle()
            gif.recycle()

            val checks = listOf(
                "frameCount" to (frames == 5),
                "dimensions" to (w == 64 && h == 64),
                "duration" to (duration > 0),
                "frameDecoded" to decodedOk,
            )
            val failed = checks.filterNot { it.second }.map { it.first }
            val msg = if (failed.isEmpty()) {
                "GIF OK (frames=$frames, ${w}x$h, duration=${duration}ms)"
            } else {
                "GIF FAIL: ${failed.joinToString(",")} " +
                    "(frames=$frames, ${w}x$h, duration=$duration, decoded=$decodedOk)"
            }
            Log.i(TAG, msg)
            msg
        } catch (t: Throwable) {
            val msg = "GIF FAIL: exception ${t.javaClass.simpleName}: ${t.message}"
            Log.e(TAG, msg, t)
            msg
        }
    }

    companion object {
        private const val TAG = "HelloGif"
    }
}
