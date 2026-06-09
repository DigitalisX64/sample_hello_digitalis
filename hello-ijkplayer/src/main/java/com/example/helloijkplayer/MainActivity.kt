package com.example.helloijkplayer

import android.graphics.SurfaceTexture
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloijkplayer.R
import tv.danmaku.ijk.media.player.IjkMediaPlayer
import java.io.File
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

/**
 * Exercises bilibili ijkplayer — an FFmpeg-based media player — under Berberis
 * ARM64->x86_64 translation. ijkplayer loads the arm64-v8a libijkffmpeg.so /
 * libijksdl.so / libijkplayer.so; the probe decodes a bundled 2s 320x240
 * H.264 + AAC clip into a detached SurfaceTexture, counting delivered video
 * frames, then self-checks prepared/duration/size/frames and logs
 * "IJKPLAYER OK" or "IJKPLAYER FAIL" so the suite's StatusTest can assert a
 * clean decode. Demux (avformat), codec init + H.264/AAC decode (avcodec) all
 * run as translated guest FFmpeg, heavily exercising NEON.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        // Decode runs off the main thread: the probe blocks on a latch waiting
        // for playback callbacks, which would ANR if run in onCreate.
        Thread {
            val result = runProbe()
            runOnUiThread { findViewById<TextView>(R.id.sample_text).text = result }
        }.start()
    }

    private fun runProbe(): String {
        return try {
            IjkMediaPlayer.loadLibrariesOnce(null)
            val clip = copyAssetToCache("test.mp4")

            val ht = HandlerThread("ijk-probe").apply { start() }
            val handler = Handler(ht.looper)
            val frames = AtomicInteger(0)
            val latch = CountDownLatch(1)

            // Detached SurfaceTexture (API 26+) needs no GL context; we only
            // count delivered frames as proof the video decoder produced output.
            val surfaceTexture = SurfaceTexture(false).apply {
                setDefaultBufferSize(320, 240)
                setOnFrameAvailableListener({ frames.incrementAndGet() }, handler)
            }
            val surface = Surface(surfaceTexture)

            // Set on the ijkplayer event thread, read on the probe thread after
            // a possible timeout, so use atomics for cross-thread visibility.
            val prepared = java.util.concurrent.atomic.AtomicBoolean(false)
            val duration = java.util.concurrent.atomic.AtomicLong(0)
            val width = AtomicInteger(0)
            val height = AtomicInteger(0)
            val rendered = java.util.concurrent.atomic.AtomicBoolean(false)
            val error = AtomicInteger(0)

            val player = IjkMediaPlayer()
            player.setSurface(surface)
            player.setOnPreparedListener { mp ->
                duration.set(mp.duration)
                width.set(mp.videoWidth)
                height.set(mp.videoHeight)
                prepared.set(true)
                mp.start()
            }
            player.setOnInfoListener { _, what, _ ->
                if (what == IjkMediaPlayer.MEDIA_INFO_VIDEO_RENDERING_START) rendered.set(true)
                false
            }
            player.setOnCompletionListener { latch.countDown() }
            player.setOnErrorListener { _, what, _ ->
                error.set(what)
                latch.countDown()
                true
            }
            player.dataSource = clip.absolutePath
            player.prepareAsync()

            // Wait for completion/error, or fall through after the clip's length
            // plus margin (the unconsumed buffer queue may stall the decoder
            // after a few frames, which is fine — we only need frames > 0).
            latch.await(8, TimeUnit.SECONDS)

            val frameCount = frames.get()
            player.release()
            surface.release()
            surfaceTexture.release()
            ht.quitSafely()

            val dur = duration.get()
            val w = width.get()
            val h = height.get()
            val checks = listOf(
                "prepared" to prepared.get(),
                "no-error" to (error.get() == 0),
                "duration" to (dur in 1500..2500),
                "videoSize" to (w == 320 && h == 240),
                "framesDecoded" to (frameCount > 0),
            )
            val failed = checks.filterNot { it.second }.map { it.first }
            val msg = if (failed.isEmpty()) {
                "IJKPLAYER OK (duration=${dur}ms, ${w}x$h, frames=$frameCount, " +
                    "renderStart=${rendered.get()})"
            } else {
                "IJKPLAYER FAIL: ${failed.joinToString(",")} " +
                    "(prepared=${prepared.get()}, duration=$dur, ${w}x$h, " +
                    "frames=$frameCount, error=${error.get()})"
            }
            Log.i(TAG, msg)
            msg
        } catch (t: Throwable) {
            val msg = "IJKPLAYER FAIL: exception ${t.javaClass.simpleName}: ${t.message}"
            Log.e(TAG, msg, t)
            msg
        }
    }

    private fun copyAssetToCache(name: String): File {
        val out = File(cacheDir, name)
        assets.open(name).use { input ->
            out.outputStream().use { input.copyTo(it) }
        }
        return out
    }

    companion object {
        private const val TAG = "HelloIjkplayer"
    }
}
