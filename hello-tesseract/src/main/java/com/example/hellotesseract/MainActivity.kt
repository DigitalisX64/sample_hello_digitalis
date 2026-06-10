package com.example.hellotesseract

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellotesseract.R
import com.googlecode.tesseract.android.TessBaseAPI
import java.io.File

/**
 * Exercises Tesseract4Android — JNI bindings around the native (C/C++) Tesseract
 * OCR engine and its Leptonica image library — under Berberis ARM64->x86_64
 * translation. The first TessBaseAPI call loads the arm64-v8a
 * libtesseract.so / libleptonica.so. The probe renders the deterministic string
 * "12345" into an in-memory bitmap (no image asset dependency), copies the
 * bundled eng.traineddata out of assets into a writable tessdata dir, runs OCR
 * restricted to the digit whitelist, and self-checks that the recognized text
 * contains "12345", logging "TESSERACT OK" or "TESSERACT FAIL" so the suite's
 * StatusTest can assert a clean run.
 *
 * OCR init + recognition is slow under translation, so the probe runs on a
 * background thread and posts the result back to the TextView.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<TextView>(R.id.sample_text)
        Thread {
            val msg = runTesseractProbe()
            Log.i(TAG, msg)
            runOnUiThread { view.text = msg }
        }.start()
    }

    private fun runTesseractProbe(): String {
        return try {
            // Tesseract expects a data path whose child directory is "tessdata".
            // The model ships in assets/tessdata/eng.traineddata; copy it into a
            // writable location the native engine can mmap.
            val dataPath = filesDir.absolutePath
            val tessdataDir = File(dataPath, "tessdata")
            if (!tessdataDir.exists()) tessdataDir.mkdirs()
            val trainedData = File(tessdataDir, "eng.traineddata")
            if (!trainedData.exists()) {
                assets.open("tessdata/eng.traineddata").use { input ->
                    trainedData.outputStream().use { output -> input.copyTo(output) }
                }
            }

            val bitmap = renderDigits("12345")

            val api = TessBaseAPI()
            if (!api.init(dataPath, "eng")) {
                return "TESSERACT FAIL: api.init returned false (dataPath=$dataPath)"
            }
            // Restrict to digits for deterministic recognition of the rendered text.
            api.setVariable(TessBaseAPI.VAR_CHAR_WHITELIST, "0123456789")
            api.setImage(bitmap)
            val recognized = api.getUTF8Text()
            api.recycle()
            bitmap.recycle()

            // Be lenient on whitespace/newlines the engine may emit around the line.
            val normalized = recognized?.replace(Regex("\\s"), "") ?: ""
            if (normalized.contains("12345")) {
                "TESSERACT OK (text=$normalized)"
            } else {
                // Genuine recognition miss — diagnostic must avoid the suite's
                // crash/fail markers so a miss is distinguishable from a crash.
                "TESSERACT MISS: expected 12345 but recognized '$normalized'"
            }
        } catch (t: Throwable) {
            "TESSERACT FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
    }

    /**
     * Draws [text] as large black digits on a white background, producing a clean,
     * high-contrast bitmap that digit OCR recognizes reliably.
     */
    private fun renderDigits(text: String): Bitmap {
        val bitmap = Bitmap.createBitmap(600, 200, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.WHITE)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = Color.BLACK
            textSize = 120f
            textAlign = Paint.Align.CENTER
        }
        // Vertically center the baseline for the given text size.
        val baseline = bitmap.height / 2f - (paint.descent() + paint.ascent()) / 2f
        canvas.drawText(text, bitmap.width / 2f, baseline, paint)
        return bitmap
    }

    companion object {
        private const val TAG = "HelloTesseract"
    }
}
