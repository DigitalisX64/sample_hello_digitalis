package com.example.hellomedia3av1

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.media3.decoder.av1.Dav1dDecoder
import androidx.media3.decoder.av1.Dav1dLibrary
import com.example.hellodigitalis.hellomedia3av1.R

/**
 * Exercises Media3's AV1 software-decoder extension under Berberis
 * ARM64->x86_64 translation. The extension wraps a native AV1 decoder behind a
 * JNI layer; the first reference to it dlopens the arm64-v8a JNI .so and runs
 * it under translation.
 *
 * The probe runs on a background thread (the native load is JNI/dlopen work, not
 * something to do on the UI thread) and does two things:
 *   1. Asserts Dav1dLibrary.isAvailable() — this loads the native library and
 *      verifies the JNI bridge resolved. This is the core translation surface.
 *   2. Constructs a Dav1dDecoder, which initialises the native decoder context
 *      (the constructor throws Dav1dDecoderException if the native side fails),
 *      then releases it. This proves the native code can run, not merely load.
 *
 * Logs "MEDIA3AV1 OK ..." on success so the suite's StatusTest can assert a
 * clean run; any genuine miss is reported without the substrings the suite
 * treats as crash markers.
 *
 * NOTE: media3 1.10.1's AV1 extension is the dav1d-based decoder (Dav1dLibrary /
 * Dav1dDecoder); the older libgav1 classes (Gav1Library / Gav1Decoder) were
 * removed after the 1.4.x line. See NOTES.md for the artifact situation — the
 * extension is source-only and its arm64-v8a JNI .so must be NDK-built.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<TextView>(R.id.sample_text)
        Thread {
            val msg = runAv1Probe()
            Handler(Looper.getMainLooper()).post { view.text = msg }
        }.start()
    }

    private fun runAv1Probe(): String {
        val msg = try {
            if (!Dav1dLibrary.isAvailable()) {
                "MEDIA3AV1 MISS: Dav1dLibrary.isAvailable() returned false " +
                    "(native libdav1d JNI did not load)"
            } else {
                // Spin up and tear down a native decoder context. Modest buffer
                // counts/sizes — we only want to prove the native init path runs
                // under translation, not stream real video.
                val decoder = Dav1dDecoder(
                    /* numInputBuffers = */ 4,
                    /* numOutputBuffers = */ 4,
                    /* initialInputBufferSize = */ 1 shl 16,
                    /* threads = */ 1,
                    /* maxFrameDelay = */ 0,
                    /* useCustomAllocator = */ false
                )
                val name = decoder.name
                decoder.release()
                "MEDIA3AV1 OK (Dav1dLibrary available, decoder \"$name\" init+release ok)"
            }
        } catch (t: Throwable) {
            "MEDIA3AV1 MISS: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloMedia3Av1"
    }
}
