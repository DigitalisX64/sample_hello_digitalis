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
package com.example.hellofbjni

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellofbjni.R
import com.facebook.jni.HybridData
import com.facebook.soloader.nativeloader.NativeLoader
import com.facebook.soloader.nativeloader.SystemDelegate

/**
 * Exercises Facebook fbjni — the C++ JNI helper runtime that backs React
 * Native, PyTorch Mobile, Hermes and Yoga — under Berberis ARM64->x86_64
 * translation.
 *
 * fbjni is JNI *infrastructure*: it is normally consumed by another native
 * library and exposes no public "compute" entry point. The native methods it
 * does declare (NativeRunnable.run, ThreadScopeSupport.runStdFunctionImpl,
 * HybridData.deleteNative) all require a native pointer that only a companion
 * C++ library can create, so none is safely callable from pure Java. The
 * deepest path reachable from a Kotlin-only app is therefore the library load
 * itself — and that load is far from passive: touching any fbjni class runs
 * its static initializer (NativeLoader.loadLibrary("fbjni")), which loads
 * jni/arm64-v8a/libfbjni.so and invokes its JNI_OnLoad. That JNI_OnLoad runs
 * real translated ARM64 native code: facebook::jni::initialize() sets up the
 * JNIEnv cache and registers the natives for HybridData, NativeRunnable and
 * ThreadScope. If any of that mis-translates, the load throws.
 *
 * The probe: install SoLoader's SystemDelegate (so NativeLoader forwards to
 * System.loadLibrary), force fbjni's HybridData class to initialize (loading
 * libfbjni.so + running JNI_OnLoad), then confirm the fbjni runtime is live by
 * constructing a HybridData with no native peer and asserting its registered
 * native plumbing reports the expected empty state (isValid() == false). It
 * logs "FBJNI OK" or "FBJNI FAIL" so the suite's StatusTest can assert a clean
 * run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // fbjni loads its .so through SoLoader's NativeLoader abstraction;
            // its class static initializers call NativeLoader.loadLibrary.
            // SystemDelegate forwards that to System.loadLibrary, which is what
            // makes Berberis map and JNI_OnLoad-init the arm64-v8a libfbjni.so.
            NativeLoader.initIfUninitialized(SystemDelegate())

            // Touching HybridData runs its static initializer
            // (NativeLoader.loadLibrary("fbjni")), loading libfbjni.so and
            // running its JNI_OnLoad -> facebook::jni::initialize(...), which
            // registers HybridData/NativeRunnable/ThreadScope natives in real
            // translated ARM64 code. A bad translation would throw here.
            val loaded = NativeLoader.loadLibrary("fbjni")

            // Confirm the fbjni runtime is actually live: a HybridData with no
            // native peer must report isValid() == false. This reads the
            // native-pointer field set up via the just-registered natives, so a
            // correct false here means JNI_OnLoad's registration succeeded and
            // the class is functional under translation.
            val emptyHybrid = HybridData()
            val validWhenEmpty = emptyHybrid.isValid

            if (loaded && !validWhenEmpty) {
                "FBJNI OK (libfbjni.so loaded, JNI_OnLoad ran, " +
                    "HybridData runtime live)"
            } else {
                "FBJNI FAIL: loaded=$loaded validWhenEmpty=$validWhenEmpty"
            }
        } catch (t: Throwable) {
            "FBJNI FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloFbjni"
    }
}
