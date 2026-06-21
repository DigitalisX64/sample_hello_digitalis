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
package com.example.hellolibxml2

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibxml2.R

/**
 * Exercises libxml2 (the GNOME XML C parser) under Berberis ARM64->x86_64
 * translation. The prefab AAR ships the prebuilt arm64-v8a libxml2.so + headers
 * but no JNI wrapper, so this module adds a small CMake + C JNI bridge
 * (src/main/cpp/native-lib.c) loaded as libhellolibxml2.so. The native probe
 * parses an embedded two-book catalog document with xmlReadMemory, walks the
 * tree (root <catalog>, two <book> children, first book's id attribute and
 * <title>/<qty> text), self-checks every value, and returns "LIBXML2 OK ..."
 * or "LIBXML2 FAIL: ..." so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val msg = try {
            // First call into the native lib loads the arm64-v8a libxml2.so and
            // runs the XML parse + tree-walk self-check.
            runProbe()
        } catch (t: Throwable) {
            "LIBXML2 FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        findViewById<TextView>(R.id.sample_text).text = msg
    }

    /** Implemented in src/main/cpp/native-lib.c (libhellolibxml2.so). */
    private external fun runProbe(): String

    companion object {
        private const val TAG = "HelloLibxml2"

        init {
            System.loadLibrary("hellolibxml2")
        }
    }
}
