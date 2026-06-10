package com.example.hellofresco

import android.net.Uri
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellofresco.R
import com.facebook.common.references.CloseableReference
import com.facebook.datasource.DataSources
import com.facebook.drawee.backends.pipeline.Fresco
import com.facebook.imagepipeline.image.CloseableBitmap
import com.facebook.imagepipeline.image.CloseableImage
import com.facebook.imagepipeline.request.ImageRequestBuilder
import kotlin.concurrent.thread

/**
 * Exercises Facebook/Meta Fresco — a native image-loading pipeline — under
 * Berberis ARM64->x86_64 translation. Fresco.initialize loads the arm64-v8a
 * native libs (libimagepipeline.so, libnative-imagetranscoder.so) via SoLoader;
 * the probe decodes a bundled 48x48 JPEG asset synchronously through Fresco's
 * native image pipeline and self-checks the produced bitmap's dimensions,
 * logging "FRESCO OK" or "FRESCO FAIL" so the suite's StatusTest can assert a
 * clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // The probe blocks on the image pipeline's DataSource, so run it off the
        // main thread; post the result back to the TextView when it finishes.
        thread(name = "fresco-probe") {
            val msg = runFrescoProbe()
            runOnUiThread {
                findViewById<TextView>(R.id.sample_text).text = msg
            }
        }
    }

    private fun runFrescoProbe(): String {
        return try {
            // Initializes SoLoader + the native image pipeline (loads the
            // arm64-v8a .so files under translation).
            Fresco.initialize(this)
            val pipeline = Fresco.getImagePipeline()

            val request = ImageRequestBuilder
                .newBuilderWithSource(Uri.parse("asset:///test.jpg"))
                .build()

            // Synchronous native decode: blocks until the pipeline produces the
            // decoded image (or throws).
            val dataSource = pipeline.fetchDecodedImage(request, this)
            val ref: CloseableReference<CloseableImage>? =
                DataSources.waitForFinalResult(dataSource)

            try {
                if (ref == null || !CloseableReference.isValid(ref)) {
                    return "FRESCO FAIL: pipeline returned no decoded image"
                }
                val image: CloseableImage = ref.get()
                val w = image.width
                val h = image.height
                // Confirm we actually got a real backing bitmap from the native
                // decoder, not just a placeholder.
                val hasBitmap = (image as? CloseableBitmap)?.underlyingBitmap != null

                if (w == 48 && h == 48 && hasBitmap) {
                    "FRESCO OK (decoded test.jpg ${w}x$h via native pipeline)"
                } else {
                    "FRESCO FAIL: expected 48x48 bitmap, got ${w}x$h hasBitmap=$hasBitmap"
                }
            } finally {
                CloseableReference.closeSafely(ref)
                dataSource.close()
            }
        } catch (t: Throwable) {
            Log.e(TAG, "probe threw", t)
            "FRESCO FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }.also { Log.i(TAG, it) }
    }

    companion object {
        private const val TAG = "HelloFresco"
    }
}
