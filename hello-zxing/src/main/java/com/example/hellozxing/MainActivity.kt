package com.example.hellozxing

import android.graphics.Bitmap
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellozxing.R
import com.google.zxing.BarcodeFormat
import com.google.zxing.MultiFormatWriter
import zxingcpp.BarcodeReader

/**
 * Exercises zxing-cpp — a native (C++) barcode decoder — under Berberis
 * ARM64->x86_64 translation. The zxing-cpp AAR ships the arm64-v8a
 * libzxingcpp_android.so; zxingcpp.BarcodeReader.read(Bitmap) decodes inside
 * that native library.
 *
 * The zxing-cpp 3.0.2 artifact provides only a reader (no writer), so the probe
 * synthesizes its test image with the pure-Java com.google.zxing encoder, then
 * decodes that bitmap with the *native* zxing-cpp reader and self-checks that
 * the decoded text and format match. Logs "ZXING OK" or "ZXING FAIL: ..." so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runZxingProbe()
    }

    private fun runZxingProbe(): String {
        val msg = try {
            val payload = "DIGITALIS-1234"

            // Encode a QR bitmap with the pure-Java zxing encoder (test fixture only).
            val bitmap = encodeQr(payload, 256)

            // Decode it with the NATIVE zxing-cpp reader — this is the code under test.
            val reader = BarcodeReader().apply {
                options = BarcodeReader.Options(
                    formats = setOf(BarcodeReader.Format.QR_CODE),
                    tryHarder = true,
                    tryRotate = true,
                )
            }
            val results = reader.read(bitmap)
            val first = results.firstOrNull()

            when {
                first == null ->
                    "ZXING FAIL: native reader found no barcode"
                first.text != payload ->
                    "ZXING FAIL: text mismatch (got \"${first.text}\", want \"$payload\")"
                first.format != BarcodeReader.Format.QR_CODE ->
                    "ZXING FAIL: format mismatch (got ${first.format}, want QR_CODE)"
                else ->
                    "ZXING OK (decoded \"${first.text}\" as ${first.format})"
            }
        } catch (t: Throwable) {
            "ZXING FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    private fun encodeQr(text: String, size: Int): Bitmap {
        val matrix = MultiFormatWriter().encode(text, BarcodeFormat.QR_CODE, size, size)
        val w = matrix.width
        val h = matrix.height
        val pixels = IntArray(w * h)
        for (y in 0 until h) {
            val row = y * w
            for (x in 0 until w) {
                pixels[row + x] = if (matrix.get(x, y)) Color.BLACK else Color.WHITE
            }
        }
        return Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888).apply {
            setPixels(pixels, 0, w, 0, 0, w, h)
        }
    }

    companion object {
        private const val TAG = "HelloZXing"
    }
}
