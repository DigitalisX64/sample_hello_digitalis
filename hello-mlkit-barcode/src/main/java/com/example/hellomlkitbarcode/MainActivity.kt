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
package com.example.hellomlkitbarcode

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellomlkitbarcode.R
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.common.InputImage
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Exercises ML Kit Barcode Scanning (bundled-model variant) under Berberis
 * ARM64->x86_64 translation. The first scanner.process() call loads the
 * arm64-v8a libbarhopper_v3.so — the fully on-device "barhopper" barcode
 * detector with its model baked into the native library (no download). The
 * probe decodes a bundled assets/qr.png QR code (known payload "DIGITALIS"),
 * which drives the heavy native image-processing detector, and self-checks
 * that the decoded rawValue == "DIGITALIS", logging "BARCODE OK" or
 * "BARCODE FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Load the bundled QR asset (a real, scannable QR encoding the
            // string "DIGITALIS") into a Bitmap.
            val bitmap: Bitmap = assets.open(ASSET_NAME).use { BitmapFactory.decodeStream(it) }
                ?: throw IllegalStateException("could not decode $ASSET_NAME")

            val scanner = BarcodeScanning.getClient()
            val image = InputImage.fromBitmap(bitmap, 0)

            // scanner.process() returns a Task; await it synchronously so the
            // probe result is ready by the time onCreate returns.
            val latch = CountDownLatch(1)
            var decoded: List<Barcode>? = null
            var failure: Throwable? = null
            scanner.process(image)
                .addOnSuccessListener { barcodes ->
                    decoded = barcodes
                    latch.countDown()
                }
                .addOnFailureListener { e ->
                    failure = e
                    latch.countDown()
                }

            if (!latch.await(AWAIT_SECONDS, TimeUnit.SECONDS)) {
                throw IllegalStateException("barcode scan timed out after ${AWAIT_SECONDS}s")
            }
            failure?.let { throw it }

            val barcodes = decoded ?: emptyList()
            val rawValue = barcodes.firstOrNull()?.rawValue

            if (rawValue == EXPECTED_PAYLOAD) {
                "BARCODE OK (decoded ${barcodes.size} barcode(s), rawValue=\"$rawValue\")"
            } else {
                "BARCODE FAIL: expected \"$EXPECTED_PAYLOAD\", got " +
                    "${barcodes.size} barcode(s) rawValue=\"$rawValue\""
            }
        } catch (t: Throwable) {
            "BARCODE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloMlkitBarcode"
        private const val ASSET_NAME = "qr.png"
        private const val EXPECTED_PAYLOAD = "DIGITALIS"
        private const val AWAIT_SECONDS = 20L
    }
}
