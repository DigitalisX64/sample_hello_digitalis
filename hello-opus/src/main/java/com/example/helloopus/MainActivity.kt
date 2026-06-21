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
package com.example.helloopus

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloopus.R
import top.oply.opuslib.OpusTool
import java.io.File
import java.io.RandomAccessFile

/**
 * Exercises the Opus audio codec — the libopus SILK/CELT fixed-point MDCT and
 * range (entropy) coder, statically linked into arm64-v8a libopustool.so — under
 * Berberis ARM64->x86_64 translation. This is a standalone codec round-trip, not
 * a media player: no AudioTrack/AudioRecord, no assets, no network.
 *
 * The techery/opus_android binding (top.oply.opuslib.OpusTool, pinned commit
 * 020c02094b) exposes a file-based native bridge: encode(wavPath, opusPath, opt)
 * runs opusenc over a WAV file and writes an Ogg/Opus file; decode(opusPath,
 * wavPath, opt) runs opusdec and writes a WAV file back. The probe:
 *
 *   1. Synthesises one 20 ms frame of 16-bit mono PCM at 48 kHz = 960 samples
 *      (a low-amplitude sine) and wraps it in a canonical 44-byte RIFF/WAVE
 *      header written to the app cacheDir.
 *   2. Calls OpusTool.encode(...) to produce an Opus packet stream, then
 *      OpusTool.decode(...) to reconstruct a WAV.
 *   3. Self-checks structural success — Opus is lossy, so PCM is NOT compared
 *      sample-for-sample: the encoded Opus file must be non-empty AND the
 *      decoded WAV must carry at least the 960 input PCM samples (opusdec trims
 *      the encoder pre-skip via granulepos, so the count lands at/near 960).
 *
 * Logs "OPUS OK ..." on success or "OPUS FAIL: ..." on any failure so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runOpusProbe()
    }

    private fun runOpusProbe(): String {
        val msg = try {
            val sampleRate = 48000
            val channels = 1
            val frameSamples = 960 // 20 ms at 48 kHz, mono

            // One 20 ms frame of low-amplitude 16-bit mono PCM (440 Hz sine).
            val pcm = ShortArray(frameSamples) { i ->
                val t = i.toDouble() / sampleRate
                (Math.sin(2.0 * Math.PI * 440.0 * t) * 8000.0).toInt().toShort()
            }

            val wavIn = File(cacheDir, "opus_probe_in.wav")
            val opusFile = File(cacheDir, "opus_probe.opus")
            val wavOut = File(cacheDir, "opus_probe_out.wav")
            listOf(wavIn, opusFile, wavOut).forEach { if (it.exists()) it.delete() }

            writeWav(wavIn, pcm, sampleRate, channels)

            val tool = OpusTool()
            // Empty option string => opusenc/opusdec defaults (VBR, auto bitrate).
            // The native bridge forwards the int result; structural file checks
            // below are the real gate (the binding's own doc and the C return
            // convention disagree on the success value, so it is logged only for
            // diagnostics, never asserted on).
            val encRc = tool.encode(wavIn.absolutePath, opusFile.absolutePath, "")
            val opusBytes = if (opusFile.exists()) opusFile.length() else 0L

            val decRc = tool.decode(opusFile.absolutePath, wavOut.absolutePath, "")
            val decodedSamples =
                if (wavOut.exists()) wavPcmSampleCount(wavOut, channels) else -1L

            val encodedNonEmpty = opusBytes > 0L
            val decodedEnough = decodedSamples >= frameSamples

            if (encodedNonEmpty && decodedEnough) {
                "OPUS OK (encoded $opusBytes bytes, decoded $decodedSamples " +
                    "samples @48kHz mono)"
            } else {
                "OPUS FAIL: encRc=$encRc decRc=$decRc encodedBytes=$opusBytes " +
                    "decodedSamples=$decodedSamples (expected >= $frameSamples)"
            }
        } catch (t: Throwable) {
            "OPUS FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    /** Writes [pcm] as a canonical 44-byte-header 16-bit PCM mono RIFF/WAVE file. */
    private fun writeWav(file: File, pcm: ShortArray, sampleRate: Int, channels: Int) {
        val bitsPerSample = 16
        val bytesPerSample = bitsPerSample / 8
        val dataSize = pcm.size * bytesPerSample
        val byteRate = sampleRate * channels * bytesPerSample
        val blockAlign = channels * bytesPerSample

        val buf = java.io.ByteArrayOutputStream(44 + dataSize)
        fun u16(v: Int) {
            buf.write(v and 0xFF); buf.write((v ushr 8) and 0xFF)
        }
        fun u32(v: Int) {
            buf.write(v and 0xFF); buf.write((v ushr 8) and 0xFF)
            buf.write((v ushr 16) and 0xFF); buf.write((v ushr 24) and 0xFF)
        }
        fun ascii(s: String) = s.forEach { buf.write(it.code) }

        ascii("RIFF"); u32(36 + dataSize); ascii("WAVE")
        ascii("fmt "); u32(16); u16(1) /* PCM */; u16(channels)
        u32(sampleRate); u32(byteRate); u16(blockAlign); u16(bitsPerSample)
        ascii("data"); u32(dataSize)
        for (s in pcm) {
            val v = s.toInt()
            buf.write(v and 0xFF); buf.write((v ushr 8) and 0xFF)
        }
        file.writeBytes(buf.toByteArray())
    }

    /**
     * Reads the `data`-chunk byte length from a 16-bit PCM RIFF/WAVE file and
     * returns the number of per-channel PCM frames it holds.
     */
    private fun wavPcmSampleCount(file: File, channels: Int): Long {
        RandomAccessFile(file, "r").use { raf ->
            val header = ByteArray(12)
            if (raf.read(header) != 12) return -1L
            // "RIFF"...."WAVE"; then walk chunk headers for "data".
            while (true) {
                val id = ByteArray(4)
                if (raf.read(id) != 4) return -1L
                val sizeBytes = ByteArray(4)
                if (raf.read(sizeBytes) != 4) return -1L
                val size = (sizeBytes[0].toInt() and 0xFF) or
                    ((sizeBytes[1].toInt() and 0xFF) shl 8) or
                    ((sizeBytes[2].toInt() and 0xFF) shl 16) or
                    ((sizeBytes[3].toInt() and 0xFF) shl 24)
                if (String(id, Charsets.US_ASCII) == "data") {
                    val bytesPerFrame = 2 * channels // 16-bit
                    return size.toLong() / bytesPerFrame
                }
                // Skip this chunk's body (chunks are word-aligned).
                raf.seek(raf.filePointer + size + (size and 1))
            }
        }
    }

    companion object {
        private const val TAG = "HelloOpus"
    }
}
