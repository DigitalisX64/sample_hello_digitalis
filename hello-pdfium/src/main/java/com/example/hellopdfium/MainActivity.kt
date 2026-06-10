package com.example.hellopdfium

import android.graphics.Bitmap
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellopdfium.R
import com.shockwave.pdfium.PdfiumCore
import java.io.File
import java.io.FileOutputStream

/**
 * Exercises PdfiumAndroid — a JNI wrapper around Google's PDFium C++ renderer —
 * under Berberis ARM64->x86_64 translation. Constructing PdfiumCore loads the
 * arm64-v8a libmodpdfium.so / libjniPdfium.so; the probe copies a bundled PDF to
 * cacheDir, opens it through a ParcelFileDescriptor, checks the page count and
 * page dimensions, renders page 0 into a Bitmap via the native engine, and
 * self-checks every result, logging "PDFIUM OK" or "PDFIUM FAIL" so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runPdfiumProbe()
    }

    private fun runPdfiumProbe(): String {
        val msg = try {
            // Copy the bundled asset to a real file so we can open it via a fd.
            val pdf = File(cacheDir, "test.pdf")
            assets.open("test.pdf").use { input ->
                FileOutputStream(pdf).use { output -> input.copyTo(output) }
            }

            val core = PdfiumCore(this)
            val fd = ParcelFileDescriptor.open(pdf, ParcelFileDescriptor.MODE_READ_ONLY)
            val doc = core.newDocument(fd)
            try {
                val pageCount = core.getPageCount(doc)
                core.openPage(doc, 0)
                val width = core.getPageWidth(doc, 0)
                val height = core.getPageHeight(doc, 0)

                // Render page 0 into a small bitmap through the native engine.
                val bmpW = 128
                val bmpH = 165
                val bitmap = Bitmap.createBitmap(bmpW, bmpH, Bitmap.Config.ARGB_8888)
                core.renderPageBitmap(doc, bitmap, 0, 0, 0, bmpW, bmpH)
                // A non-blank render leaves at least one non-transparent pixel.
                var rendered = false
                val pixels = IntArray(bmpW * bmpH)
                bitmap.getPixels(pixels, 0, bmpW, 0, 0, bmpW, bmpH)
                for (p in pixels) {
                    if (p != 0) { rendered = true; break }
                }

                val checks = listOf(
                    "pageCount" to (pageCount == 1),
                    "width" to (width > 0),
                    "height" to (height > 0),
                    "bitmap" to (bitmap.width == bmpW && bitmap.height == bmpH),
                    "rendered" to rendered,
                )
                val failed = checks.filterNot { it.second }.map { it.first }
                if (failed.isEmpty()) {
                    "PDFIUM OK (pages=$pageCount, ${width}x$height px)"
                } else {
                    "PDFIUM FAIL: ${failed.joinToString(",")}"
                }
            } finally {
                core.closeDocument(doc)
            }
        } catch (t: Throwable) {
            "PDFIUM FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloPdfium"
    }
}
