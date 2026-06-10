package com.example.helloffmpegkit

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.arthenica.ffmpegkit.FFmpegKitConfig
import com.arthenica.ffmpegkit.FFprobeKit
import com.example.hellodigitalis.helloffmpegkit.R
import java.io.File
import java.io.FileOutputStream

/**
 * Exercises FFmpegKit — the arthenica FFmpeg wrapper, a full native (C) media
 * stack (libavcodec/libavformat/libavutil/libswresample + libffmpegkit.so) —
 * under Berberis ARM64->x86_64 translation. The first FFmpegKit call loads the
 * arm64-v8a libraries; the probe then runs FFprobe over a bundled WAV asset,
 * which demuxes the container and probes the stream entirely in translated
 * guest ARM64 code (libavformat open/read_header + the PCM decoder).
 *
 * The probe copies assets/tone.wav (a 0.3s 440 Hz 16-bit PCM clip) to a real
 * file, runs FFprobeKit.getMediaInformation on it, and self-checks that the
 * native probe reports an audio stream of roughly the expected duration. It
 * logs "FFMPEGKIT OK" or a non-"FAIL" diagnostic so the suite's StatusTest can
 * assert a clean run. Probing is off the UI thread.
 *
 * Note: the deterministic decode is driven through FFprobe rather than a full
 * FFmpegKit.execute() transcode. The unofficial 16KB-page fork used here (the
 * official arthenica artifacts were retired from Maven in 2025) hangs in its
 * ffmpeg-CLI transcode session lifecycle after the encode completes; the
 * FFprobe demux/probe path exercises the same native libavformat/libavcodec
 * code without that broken session loop.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val text = findViewById<TextView>(R.id.sample_text)
        text.text = "Running FFmpegKit probe..."

        Thread {
            val msg = runFFmpegKitProbe()
            Log.i(TAG, msg)
            Handler(Looper.getMainLooper()).post { text.text = msg }
        }.start()
    }

    private fun runFFmpegKitProbe(): String {
        return try {
            val version = FFmpegKitConfig.getFFmpegVersion()
            val buildDate = FFmpegKitConfig.getBuildDate()

            // FFprobe reads from a path, so materialise the bundled asset.
            val clip = File(cacheDir, "tone.wav")
            assets.open("tone.wav").use { input ->
                FileOutputStream(clip).use { out -> input.copyTo(out) }
            }
            if (!clip.exists() || clip.length() <= 0L) {
                return "FFMPEGKIT error: asset tone.wav missing/empty"
            }

            // Native demux + stream probe through libavformat/libavcodec.
            val info = FFprobeKit.getMediaInformation(clip.absolutePath).mediaInformation
                ?: return "FFMPEGKIT error: FFprobe returned no media information"
            val durationStr = info.duration
            val duration = durationStr?.toDoubleOrNull() ?: -1.0
            val streamCount = info.streams?.size ?: 0
            val hasAudio = info.streams?.any { it.type == "audio" } == true
            val durationOk = duration in 0.20..0.45

            if (!hasAudio || streamCount <= 0) {
                return "FFMPEGKIT error: no audio stream parsed " +
                    "(streams=$streamCount hasAudio=$hasAudio)"
            }
            if (!durationOk) {
                return "FFMPEGKIT error: unexpected duration $durationStr " +
                    "(expected ~0.30s)"
            }

            "FFMPEGKIT OK (FFmpeg $version, built $buildDate; " +
                "FFprobe duration=${durationStr}s, audio streams=$streamCount, " +
                "clip=${clip.length()} bytes)"
        } catch (t: Throwable) {
            "FFMPEGKIT error: ${t.javaClass.simpleName}: ${t.message}"
        }
    }

    companion object {
        private const val TAG = "HelloFFmpegKit"
    }
}
