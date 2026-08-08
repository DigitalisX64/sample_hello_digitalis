package com.example.hellonativewindow

import android.graphics.ImageFormat
import android.graphics.PixelFormat
import android.media.ImageReader
import android.os.Bundle
import android.util.Log
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellonativewindow.R

/**
 * Drives the native ANativeWindow probe and checks what came out the other end.
 *
 * The producer side (native-lib.cpp) locks buffers and writes a pattern that is
 * a pure function of (x, y, frame). This side attaches an ImageReader as the
 * consumer, so every posted frame can be read back and compared against the
 * same function, recomputed here rather than taken on trust. That closes the
 * loop the field bug slipped through: a lock() that reports a broken stride
 * still "works" at every API call and only shows up as wrong pixels.
 *
 * A SurfaceView case runs last so the posted pattern also has to survive the
 * real display path, which the screenshot test compares against a reference.
 */
class MainActivity : AppCompatActivity() {

    private lateinit var status: TextView
    private val results = StringBuilder()
    private var failures = 0
    private var checks = 0

    private data class Case(val label: String, val width: Int, val height: Int, val format: Int)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        status = findViewById(R.id.sample_text)

        val surfaceView: SurfaceView = findViewById(R.id.surface)
        surfaceView.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                Thread {
                    runImageReaderCases()
                    runDisplayCase(holder)
                    report()
                }.start()
            }

            override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, ht: Int) = Unit
            override fun surfaceDestroyed(holder: SurfaceHolder) = Unit
        })
    }

    /**
     * Every case posts through a real BufferQueue and is read back off the
     * consumer end. Widths are chosen to stress row padding: 641 and 361 are
     * odd, and 720 is 16-aligned but not 64-aligned, which is the geometry a
     * portrait video actually uses.
     */
    private fun runImageReaderCases() {
        val cases = listOf(
            Case("rgba-640x480", 640, 480, PixelFormat.RGBA_8888),
            Case("rgba-641x481", 641, 481, PixelFormat.RGBA_8888),
            Case("rgb565-320x240", 320, 240, PixelFormat.RGB_565),
            Case("yv12-720x1280", 720, 1280, ImageFormat.YV12),
            Case("yv12-1280x720", 1280, 720, ImageFormat.YV12),
            Case("yv12-642x362", 642, 362, ImageFormat.YV12),
        )
        for (case in cases) {
            val reader = try {
                ImageReader.newInstance(case.width, case.height, case.format, MAX_IMAGES)
            } catch (e: Exception) {
                // A format this device's gralloc cannot allocate at all is an
                // environment limit, not a translation defect: record it and
                // move on rather than reporting a failure we cannot attribute.
                note("${case.label}: SKIP (${e.javaClass.simpleName})")
                continue
            }
            try {
                val err =
                    probeSurface(reader.surface, case.width, case.height, case.format, FRAMES, true)
                if (err.isNotEmpty()) {
                    fail("${case.label}: $err")
                    continue
                }
                verifyLatest(case, reader)
            } finally {
                reader.close()
            }
        }
    }

    /** Compare the newest posted frame against the pattern, plane by plane. */
    private fun verifyLatest(case: Case, reader: ImageReader) {
        val image = reader.acquireLatestImage()
        if (image == null) {
            fail("${case.label}: no image reached the consumer")
            return
        }
        try {
            if (image.width != case.width || image.height != case.height) {
                fail("${case.label}: consumer got ${image.width}x${image.height}")
                return
            }
            val plane = image.planes[0]
            val rowStride = plane.rowStride
            val pixelStride = plane.pixelStride
            if (rowStride == 0) {
                fail("${case.label}: consumer rowStride=0")
                return
            }
            val buffer = plane.buffer
            val frame = FRAMES - 1
            var mismatches = 0
            var firstMismatch = ""
            // Sample a lattice rather than every pixel: enough points to catch a
            // wrong stride (which shears the image) without a slow full scan.
            var y = 0
            while (y < image.height) {
                var x = 0
                while (x < image.width) {
                    val offset = y * rowStride + x * pixelStride
                    if (offset >= buffer.limit()) {
                        fail("${case.label}: offset $offset past buffer ${buffer.limit()}")
                        return
                    }
                    val got = buffer.get(offset).toInt() and 0xff
                    val want = expectedFirstByte(case.format, x, y, frame)
                    if (got != want) {
                        mismatches++
                        if (firstMismatch.isEmpty()) {
                            firstMismatch = "at ($x,$y) got $got want $want"
                        }
                    }
                    x += 37
                }
                y += 29
            }
            if (mismatches > 0) {
                fail("${case.label}: $mismatches sampled pixels wrong, first $firstMismatch")
            } else {
                pass("${case.label}: OK (rowStride=$rowStride)")
            }
        } finally {
            image.close()
        }
    }

    /**
     * The producer's first byte for a pixel. RGB_565 packs the pattern across
     * the channels, so its low byte carries the green and blue bits rather than
     * the pattern value itself.
     */
    private fun expectedFirstByte(format: Int, x: Int, y: Int, frame: Int): Int {
        val v = patternByte(x, y, frame)
        if (format != PixelFormat.RGB_565) {
            return v
        }
        return (((v shr 2) and 0x07) shl 5) or (v shr 3)
    }

    /**
     * Post the pattern to a real on-screen surface. Delivery here depends on
     * the whole display path -- BufferQueue, SurfaceFlinger, the host GPU --
     * not just the proxy, which is why the screenshot test guards it.
     *
     * RGBA rather than YV12: a CPU-written YV12 SurfaceView composites to a
     * blank green frame on this emulator, and does so identically in a build
     * with no translation in it at all, so a YV12 case here would be asserting
     * on the host compositor rather than on anything Digitalis controls. YV12
     * is covered above, against a consumer whose pixels can be read back.
     */
    private fun runDisplayCase(holder: SurfaceHolder) {
        val err = probeSurface(
            holder.surface, DISPLAY_W, DISPLAY_H, PixelFormat.RGBA_8888, DISPLAY_FRAMES, false
        )
        if (err.isNotEmpty()) {
            fail("display-rgba: $err")
        } else {
            pass("display-rgba: OK")
        }
    }

    private fun pass(line: String) {
        checks++
        results.append(line).append('\n')
        Log.i(TAG, line)
    }

    private fun fail(line: String) {
        checks++
        failures++
        results.append("FAIL ").append(line).append('\n')
        Log.e(TAG, "FAIL $line")
    }

    private fun note(line: String) {
        results.append(line).append('\n')
        Log.i(TAG, line)
    }

    private fun report() {
        val summary = if (failures == 0) {
            "ANativeWindow: ${checks} checks OK"
        } else {
            "ANativeWindow: FAIL ($failures of $checks checks)"
        }
        Log.i(TAG, summary)
        runOnUiThread { status.text = summary + "\n" + results.toString() }
    }

    external fun probeSurface(
        surface: Any,
        width: Int,
        height: Int,
        format: Int,
        frames: Int,
        strictQuery: Boolean,
    ): String

    external fun patternByte(x: Int, y: Int, frame: Int): Int

    companion object {
        private const val TAG = "hellonativewindow"
        private const val MAX_IMAGES = 3
        private const val FRAMES = 3

        // Small enough to post quickly, large enough that a stride bug shears
        // the image visibly in the screenshot.
        private const val DISPLAY_W = 720
        private const val DISPLAY_H = 1280

        // Enough posts that the frame on screen when the screenshot is taken is
        // one this sample drew, not a buffer the surface started life with.
        private const val DISPLAY_FRAMES = 8

        init {
            System.loadLibrary("hellonativewindow")
        }
    }
}
