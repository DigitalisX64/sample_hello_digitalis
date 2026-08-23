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
package com.example.hellohiddenapibypass

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellohiddenapibypass.R
import org.lsposed.hiddenapibypass.HiddenApiBypass

/**
 * Exercises LSPosed HiddenApiBypass under Berberis ARM64->x86_64 translation.
 *
 * HiddenApiBypass is a pure-Java/Kotlin library (its AAR ships no native .so).
 * On Android P+ the runtime denies reflective access to @hide framework APIs;
 * the library lifts that restriction by reflecting into VMRuntime and using
 * sun.misc.Unsafe to install exemptions. That Unsafe/reflection machinery is
 * exactly the kind of code path real ARM64 apps (e.g. NetEase Cloud Music)
 * drive through the translator, so the probe:
 *
 *   1. installs a blanket exemption via addHiddenApiExemptions("L"),
 *   2. proves the exemption works by reflectively reaching a @hide framework
 *      API (android.os.SystemProperties#get) that would otherwise be denied,
 *   3. also drives the library's own reflection helper (HiddenApiBypass.invoke)
 *      and cross-checks the two results agree,
 *
 * logging "HIDDENAPIBYPASS OK" on success or "HIDDENAPIBYPASS FAIL: <reason>"
 * so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // 1. Drive the library's core: install a hidden-API exemption for
            //    every class ("L" is the prefix of every JVM class descriptor).
            //    Internally this reflects into VMRuntime and uses Unsafe.
            val exemptOk = HiddenApiBypass.addHiddenApiExemptions("L")

            // 2. Prove the exemption actually works. android.os.SystemProperties
            //    is a @hide framework class; reflectively reaching its get(String)
            //    method and invoking it would be denied by hidden-API enforcement
            //    without the exemption above.
            val sp = Class.forName("android.os.SystemProperties")
            val getMethod = sp.getMethod("get", String::class.java)
            val sdkViaReflection = getMethod.invoke(null, "ro.build.version.sdk") as String

            // 3. Also drive the library's own reflection helper, the entry point
            //    real apps call, and cross-check it returns the same value.
            val sdkViaLib = HiddenApiBypass.invoke(
                sp, null, "get", "ro.build.version.sdk"
            ) as String

            val reflectionOk = sdkViaReflection.isNotEmpty()
            val libOk = sdkViaLib == sdkViaReflection

            if (exemptOk && reflectionOk && libOk) {
                "HIDDENAPIBYPASS OK (exempt=$exemptOk, " +
                    "SystemProperties.get(ro.build.version.sdk)=$sdkViaReflection " +
                    "via both plain reflection and HiddenApiBypass.invoke)"
            } else {
                "HIDDENAPIBYPASS FAIL: exempt=$exemptOk reflection='$sdkViaReflection' " +
                    "lib='$sdkViaLib'"
            }
        } catch (t: Throwable) {
            "HIDDENAPIBYPASS FAIL: ${t.javaClass.name}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloHiddenApiBypass"
    }
}
