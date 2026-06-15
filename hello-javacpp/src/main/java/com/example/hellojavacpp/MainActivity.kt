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

package com.example.hellojavacpp

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellojavacpp.R
import org.bytedeco.javacpp.BytePointer
import org.bytedeco.javacpp.IntPointer
import org.bytedeco.javacpp.Loader

/**
 * Exercises the JavaCPP runtime — the native pointer / off-heap-memory layer
 * that every Bytedeco binding (OpenCV, FFmpeg, ...) builds on — under Berberis
 * ARM64->x86_64 translation. Loader.load() pulls in the arm64-v8a
 * libjnijavacpp.so; the probe then allocates native (malloc'd, off-heap)
 * BytePointer and IntPointer buffers, writes a known pattern through JavaCPP's
 * native put(index, value) accessors, reads every element back through the
 * native get(index) accessors, verifies the round-trip is exact, and frees the
 * buffers with deallocate(). This drives JavaCPP's JNI peer methods (the raw
 * native memory read/write/alloc/free path), logging "JAVACPP OK" or
 * "JAVACPP FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Force the JavaCPP native library (libjnijavacpp.so) to load. This
            // is the first call into the bionic-linked arm64-v8a native; it
            // returns the absolute path of the .so it loaded. Pass the core
            // Pointer type explicitly: the no-arg Loader.load() infers the
            // library from the calling class, which for a plain Activity
            // resolves to the wrong name (libjniContext.so) and throws
            // UnsatisfiedLinkError.
            val loaded = Loader.load(BytePointer::class.java)

            // --- BytePointer: allocate N bytes off-heap, write & read a pattern.
            val byteCount = 4096L
            val bytes = BytePointer(byteCount)
            val byteAddr = bytes.address()
            var byteOk = byteAddr != 0L && bytes.capacity() >= byteCount
            // Write through the native put(long, byte) accessor...
            var i = 0L
            while (i < byteCount) {
                bytes.put(i, ((i * 31 + 7) and 0xFF).toByte())
                i++
            }
            // ...and read every element back through native get(long).
            i = 0L
            while (byteOk && i < byteCount) {
                val expected = ((i * 31 + 7) and 0xFF).toByte()
                if (bytes.get(i) != expected) byteOk = false
                i++
            }
            bytes.deallocate()

            // --- IntPointer: allocate N ints off-heap, write & read a pattern.
            val intCount = 1024L
            val ints = IntPointer(intCount)
            val intAddr = ints.address()
            var intOk = intAddr != 0L && ints.capacity() >= intCount
            var j = 0L
            while (j < intCount) {
                ints.put(j, (j * 2654435761L).toInt())
                j++
            }
            j = 0L
            while (intOk && j < intCount) {
                val expected = (j * 2654435761L).toInt()
                if (ints.get(j) != expected) intOk = false
                j++
            }
            ints.deallocate()

            if (byteOk && intOk) {
                "JAVACPP OK (loaded ${shortName(loaded)}; " +
                    "byte[$byteCount]@0x${byteAddr.toString(16)} + " +
                    "int[$intCount]@0x${intAddr.toString(16)} round-tripped)"
            } else {
                "JAVACPP FAIL: byteOk=$byteOk intOk=$intOk " +
                    "byteAddr=0x${byteAddr.toString(16)} intAddr=0x${intAddr.toString(16)}"
            }
        } catch (t: Throwable) {
            "JAVACPP FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    private fun shortName(path: String?): String =
        path?.substringAfterLast('/') ?: "<null>"

    companion object {
        private const val TAG = "HelloJavacpp"
    }
}
