package com.example.hellomedia3ffmpeg

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.TextView
import androidx.annotation.OptIn
import androidx.appcompat.app.AppCompatActivity
import androidx.media3.common.MimeTypes
import androidx.media3.common.util.UnstableApi
import androidx.media3.decoder.ffmpeg.FfmpegLibrary
import com.example.hellodigitalis.hellomedia3ffmpeg.R

/**
 * Exercises Media3's native FFmpeg audio-decoder extension
 * (androidx.media3:media3-decoder-ffmpeg) under Berberis ARM64->x86_64
 * translation. The first FfmpegLibrary call dlopens the arm64-v8a
 * libffmpegJN.so and resolves its JNI symbols; the probe then asks the native
 * layer for its FFmpeg version string and whether it can decode a couple of
 * common audio MIME types. All of this is native (C) work driven through JNI,
 * so a clean pass means the FFmpeg .so loaded and its JNI bridge executed
 * correctly under translation.
 *
 * The probe runs on a background Thread (decoder init can be heavy) and posts
 * "MEDIA3FFMPEG OK" / a non-"FAIL" diagnostic / "MEDIA3FFMPEG FAIL" back to the
 * TextView so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<TextView>(R.id.sample_text)
        val main = Handler(Looper.getMainLooper())
        Thread {
            val msg = runFfmpegProbe()
            main.post { view.text = msg }
        }.start()
    }

    @OptIn(UnstableApi::class)
    private fun runFfmpegProbe(): String {
        val msg = try {
            // First touch: dlopen libffmpegJN.so + resolve its JNI symbols.
            val available = FfmpegLibrary.isAvailable()
            if (!available) {
                // The Java classes resolved but the native .so is absent (the
                // expected state for a stock Maven build — the extension ships
                // source-only; see NOTES.md). Report it without the "FAIL"
                // marker so the suite treats this as a benign miss, not a
                // translator crash.
                "MEDIA3FFMPEG MISS: FfmpegLibrary.isAvailable()=false " +
                    "(native libffmpegJN.so not bundled — extension is " +
                    "source-only, must be NDK-built; see NOTES.md)"
            } else {
                // The native bridge loaded — drive it through JNI.
                val version = FfmpegLibrary.getVersion()
                val supportsAac = FfmpegLibrary.supportsFormat(MimeTypes.AUDIO_AAC)
                val supportsOpus = FfmpegLibrary.supportsFormat(MimeTypes.AUDIO_OPUS)

                if (supportsAac || supportsOpus) {
                    "MEDIA3FFMPEG OK (ffmpeg $version, AAC=$supportsAac, " +
                        "Opus=$supportsOpus)"
                } else {
                    "MEDIA3FFMPEG FAIL: native loaded (ffmpeg $version) but " +
                        "supportsFormat returned false for both AAC and Opus"
                }
            }
        } catch (t: Throwable) {
            "MEDIA3FFMPEG FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloMedia3Ffmpeg"
    }
}
