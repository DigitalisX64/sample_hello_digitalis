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
package com.example.hellomupdf

import android.graphics.Bitmap
import android.os.Bundle
import android.util.Base64
import android.util.Log
import android.widget.ImageView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.artifex.mupdf.fitz.ColorSpace
import com.artifex.mupdf.fitz.Document
import com.artifex.mupdf.fitz.Matrix
import com.artifex.mupdf.fitz.Page
import com.artifex.mupdf.fitz.Pixmap
import com.example.hellodigitalis.hellomupdf.R

/**
 * Exercises MuPDF (Artifex) fitz — a native (C) PDF/vector/font rendering engine,
 * a different codebase from PDFium — under Berberis ARM64->x86_64 translation. The
 * first MuPDF call loads the arm64-v8a libmupdf_java.so; the probe opens a small,
 * hand-built single-page PDF (a large black filled rectangle on a 612x792 page),
 * rasterizes page 0 to an ARGB bitmap at 2x scale, then self-checks that the output
 * is actually rendered by counting non-white pixels: a working renderer must paint
 * a meaningful fraction (the black rectangle covers ~73% of the page). It logs
 * "MUPDF OK" on success or "MUPDF FAIL" on any error so the suite's StatusTest can
 * assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val (msg, bitmap) = runMupdfProbe()
        findViewById<TextView>(R.id.sample_text).text = msg
        if (bitmap != null) {
            findViewById<ImageView>(R.id.sample_image).setImageBitmap(bitmap)
        }
    }

    private fun runMupdfProbe(): Pair<String, Bitmap?> {
        var bitmap: Bitmap? = null
        val msg = try {
            // A minimal, valid single-page PDF (PDF-1.4, xref + trailer) whose
            // content stream fills a large black rectangle (50 50 512 692) over a
            // 612x792 page — clearly visible, non-white vector content.
            val pdfBytes = Base64.decode(PDF_BASE64, Base64.DEFAULT)

            // Open directly from bytes; the second arg is a filename/magic hint
            // MuPDF uses to pick the document handler.
            val doc: Document = Document.openDocument(pdfBytes, "sample.pdf")
            try {
                val pageCount = doc.countPages()
                if (pageCount < 1) {
                    return "MUPDF FAIL: document reports $pageCount pages" to null
                }

                val page: Page = doc.loadPage(0)
                try {
                    // Render page 0 at 2x scale into an RGBA pixmap. getPixels()
                    // requires an alpha channel (RGB/BGR-with-alpha), so the pixmap
                    // is created with alpha=true; MuPDF clears it to transparent and
                    // rasterizes the page content opaquely on top.
                    val scale = Matrix.Scale(2.0f)
                    val pixmap: Pixmap =
                        page.toPixmap(scale, ColorSpace.DeviceRGB, /* alpha = */ true)
                    try {
                        val w = pixmap.width
                        val h = pixmap.height
                        // getPixels() returns w*h packed ARGB ints.
                        val pixels: IntArray = pixmap.pixels
                        if (pixels.size != w * h) {
                            return ("MUPDF FAIL: pixel buffer ${pixels.size} != " +
                                "$w*$h=${w * h}") to null
                        }

                        // Count actually-drawn dark content: opaque, non-white
                        // pixels. Transparent (un-rendered) background is skipped, so
                        // this fails a blank raster instead of mistaking transparent
                        // for content.
                        var nonWhite = 0
                        for (p in pixels) {
                            val a = (p ushr 24) and 0xFF
                            val r = (p ushr 16) and 0xFF
                            val g = (p ushr 8) and 0xFF
                            val b = p and 0xFF
                            if (a != 0 && (r < 250 || g < 250 || b < 250)) {
                                nonWhite++
                            }
                        }
                        val total = w * h
                        val pct = if (total > 0) 100.0 * nonWhite / total else 0.0

                        // Copy into an ARGB_8888 Bitmap for display (the self-check
                        // already ran on the pixmap samples above).
                        bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
                        // pixmap RGB ints have alpha=0; force opaque for display.
                        for (i in pixels.indices) {
                            pixels[i] = pixels[i] or (0xFF shl 24)
                        }
                        bitmap.setPixels(pixels, 0, w, 0, 0, w, h)

                        if (pct > 5.0) {
                            "MUPDF OK (rendered ${w}x${h}, ${"%.1f".format(pct)}% non-white)"
                        } else {
                            "MUPDF FAIL: page rendered ${w}x${h} but only " +
                                "${"%.1f".format(pct)}% non-white (expected > 5%)"
                        }
                    } finally {
                        pixmap.destroy()
                    }
                } finally {
                    page.destroy()
                }
            } finally {
                doc.destroy()
            }
        } catch (t: Throwable) {
            "MUPDF FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg to bitmap
    }

    companion object {
        private const val TAG = "HelloMupdf"

        // A hand-built, validated single-page PDF: %PDF-1.4 header, 4 objects
        // (Catalog, Pages, Page with MediaBox [0 0 612 792], Contents stream
        // "0 0 0 rg / 50 50 512 692 re / f"), xref table, trailer, %%EOF. The
        // black fill covers ~73% of the page, well above the 5% non-white gate.
        private const val PDF_BASE64 =
            "JVBERi0xLjQKJeLjz9MKMSAwIG9iago8PCAvVHlwZSAvQ2F0YWxvZyAvUGFnZXMgMiAwIFIgPj4K" +
            "ZW5kb2JqCjIgMCBvYmoKPDwgL1R5cGUgL1BhZ2VzIC9LaWRzIFszIDAgUl0gL0NvdW50IDEgPj4K" +
            "ZW5kb2JqCjMgMCBvYmoKPDwgL1R5cGUgL1BhZ2UgL1BhcmVudCAyIDAgUiAvTWVkaWFCb3ggWzAg" +
            "MCA2MTIgNzkyXSAvQ29udGVudHMgNCAwIFIgL1Jlc291cmNlcyA8PCA+PiA+PgplbmRvYmoKNCAw" +
            "IG9iago8PCAvTGVuZ3RoIDI4ID4+CnN0cmVhbQowIDAgMCByZwo1MCA1MCA1MTIgNjkyIHJlCmYK" +
            "CmVuZHN0cmVhbQplbmRvYmoKeHJlZgowIDUKMDAwMDAwMDAwMCA2NTUzNSBmIAowMDAwMDAwMDE1" +
            "IDAwMDAwIG4gCjAwMDAwMDAwNjQgMDAwMDAgbiAKMDAwMDAwMDEyMSAwMDAwMCBuIAowMDAwMDAw" +
            "MjI1IDAwMDAwIG4gCnRyYWlsZXIKPDwgL1NpemUgNSAvUm9vdCAxIDAgUiA+PgpzdGFydHhyZWYK" +
            "MzAzCiUlRU9GCg=="
    }
}
