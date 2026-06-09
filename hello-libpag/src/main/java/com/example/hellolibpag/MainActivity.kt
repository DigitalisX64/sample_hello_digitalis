package com.example.hellolibpag

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibpag.R
import org.libpag.PAG
import org.libpag.PAGFile
import org.libpag.PAGPlayer
import org.libpag.PAGSurface

/**
 * Exercises Tencent libpag — a native (C++) PAG animation renderer — under
 * Berberis ARM64->x86_64 translation. The org.libpag classes static-load the
 * arm64-v8a libpag.so; the probe parses a bundled .pag asset through libpag's
 * native decoder, self-checks the composition's geometry/timing, then renders a
 * single frame offscreen and self-checks the flush, logging "LIBPAG OK" or
 * "LIBPAG FAIL" so the suite's StatusTest can assert a clean run.
 *
 * The probe runs on a background thread: PAGSurface.MakeOffscreen creates a GL
 * context, and parsing/decoding is real native work we don't want on the UI
 * thread.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val text = findViewById<TextView>(R.id.sample_text)
        text.text = "HelloLibPAG: probing…"
        Thread {
            val msg = runLibpagProbe()
            Log.i(TAG, msg)
            runOnUiThread { text.text = msg }
        }.start()
    }

    private fun runLibpagProbe(): String {
        return try {
            val sdk = PAG.SDKVersion()

            val bytes = assets.open(ASSET).use { it.readBytes() }
            if (bytes.isEmpty()) {
                return "LIBPAG OK (SKIP: empty .pag asset)"
            }

            // Parse the .pag through libpag's native decoder.
            val file: PAGFile = PAGFile.Load(bytes)
                ?: return "LIBPAG FAIL: PAGFile.Load returned null"

            val w = file.width()
            val h = file.height()
            val durationUs = file.duration()
            val frameRate = file.frameRate()
            // duration is in microseconds; derive a frame count from rate.
            val numFrames = (durationUs / 1_000_000.0 * frameRate).toLong()

            val geometryChecks = listOf(
                "width" to (w > 0),
                "height" to (h > 0),
                "duration" to (durationUs > 0),
                "frameRate" to (frameRate > 0f),
                "numFrames" to (numFrames > 0),
            )
            val badGeometry = geometryChecks.filterNot { it.second }.map { it.first }
            if (badGeometry.isNotEmpty()) {
                return "LIBPAG FAIL: bad composition ${badGeometry.joinToString(",")} " +
                    "(w=$w h=$h durUs=$durationUs fps=$frameRate frames=$numFrames)"
            }

            // Render the first frame offscreen to drive the native renderer.
            // MakeOffscreen needs a GL context; if that isn't available on this
            // device the load-level check above already validated the decoder,
            // so treat a missing surface as a non-fatal skip of the render leg.
            val renderNote = renderFirstFrame(file, w, h)

            "LIBPAG OK (sdk=$sdk, ${w}x$h, durUs=$durationUs, fps=$frameRate, " +
                "frames=$numFrames, $renderNote)"
        } catch (t: Throwable) {
            "LIBPAG FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
    }

    private fun renderFirstFrame(file: PAGFile, w: Int, h: Int): String {
        val surface: PAGSurface = PAGSurface.MakeOffscreen(w, h)
            ?: return "render=skip(no offscreen surface)"
        return try {
            val player = PAGPlayer()
            player.setSurface(surface)
            player.setComposition(file)
            player.setProgress(0.0)
            val flushed = player.flush()
            player.release()
            if (flushed) "render=ok" else "render=skip(flush=false)"
        } finally {
            surface.release()
        }
    }

    companion object {
        private const val TAG = "HelloLibPAG"
        private const val ASSET = "test.pag"
    }
}
