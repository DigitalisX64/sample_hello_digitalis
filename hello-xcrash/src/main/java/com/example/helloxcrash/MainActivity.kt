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
package com.example.helloxcrash

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloxcrash.R
import xcrash.XCrash
import java.io.File

/**
 * Exercises iQIYI xCrash — a native crash-capture library — under Berberis
 * ARM64->x86_64 translation. XCrash.init() loads the arm64-v8a libxcrash.so
 * and libxcrash_dumper.so and installs native signal (SIGSEGV/SIGABRT/...) and
 * ANR handlers, which is the real native path we want to run through the
 * translator. The probe initializes xCrash and confirms the native library
 * actually mapped into the process WITHOUT triggering a crash (a real crash
 * would fail the test), logging "XCRASH OK" or "XCRASH FAIL" so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runXCrashProbe()
    }

    private fun runXCrashProbe(): String {
        val msg = try {
            // Loads libxcrash.so + libxcrash_dumper.so and installs the native
            // crash/ANR handlers. Returns 0 (Errno.OK) on success.
            val initResult = XCrash.init(this)

            // Confirm the native library actually mapped into this process:
            // proof the .so loaded and ran its JNI_OnLoad under translation.
            val mapped = File("/proc/self/maps").readText().contains("libxcrash.so")

            if (initResult == 0 && mapped) {
                "XCRASH OK (init=$initResult, libxcrash mapped)"
            } else {
                "XCRASH FAIL: init=$initResult mapped=$mapped"
            }
        } catch (t: Throwable) {
            "XCRASH FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloXcrash"
    }
}
