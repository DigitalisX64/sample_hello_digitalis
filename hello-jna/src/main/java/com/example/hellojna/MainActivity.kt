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
package com.example.hellojna

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellojna.R
import com.sun.jna.Library
import com.sun.jna.Native

/**
 * Exercises Java Native Access (JNA) under Berberis ARM64->x86_64 translation.
 *
 * JNA is a runtime FFI layer: instead of hand-written JNI stubs, it loads a
 * native shared library and builds a libffi call frame for each invocation,
 * marshalling Java arguments into native registers/stack per the C calling
 * convention and reading the result back. The first call here pulls in the
 * arm64-v8a libjnidispatch.so bundled in the JNA AAR, then maps a handful of
 * bionic libc functions and calls them. This stresses libffi's argument
 * marshalling and call-frame setup (the FFI calling convention) across the
 * translation boundary.
 *
 * The probe maps strlen / abs / labs / toupper from "c" (bionic libc) and
 * self-checks each result, logging "JNA OK" or "JNA FAIL" so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    /** A subset of bionic libc, mapped through JNA's libffi dispatch. */
    interface CLib : Library {
        fun strlen(s: String): Long
        fun abs(i: Int): Int
        fun labs(l: Long): Long
        fun toupper(c: Int): Int
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Loading "c" resolves bionic libc and triggers libjnidispatch.so
            // (arm64-v8a) to load; every method call below is a libffi dispatch.
            val libc = Native.load("c", CLib::class.java)

            val strlenVal = libc.strlen("hello")       // expect 5
            val absVal = libc.abs(-7)                   // expect 7
            val labsVal = libc.labs(-1234567890123L)    // expect 1234567890123
            val toupperVal = libc.toupper('a'.code)     // expect 'A' (65)

            val checks = listOf(
                "strlen" to (strlenVal == 5L),
                "abs" to (absVal == 7),
                "labs" to (labsVal == 1234567890123L),
                "toupper" to (toupperVal == 'A'.code),
            )
            val failed = checks.filterNot { it.second }.map { it.first }

            if (failed.isEmpty()) {
                "JNA OK (strlen=$strlenVal abs=$absVal labs=$labsVal " +
                    "toupper=$toupperVal via libffi)"
            } else {
                "JNA FAIL: wrong result(s) ${failed.joinToString(",")} " +
                    "(strlen=$strlenVal abs=$absVal labs=$labsVal toupper=$toupperVal)"
            }
        } catch (t: Throwable) {
            "JNA FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloJna"
    }
}
