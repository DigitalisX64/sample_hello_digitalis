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
package com.example.hellorive

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import app.rive.runtime.kotlin.core.File
import app.rive.runtime.kotlin.core.Rive
import com.example.hellodigitalis.hellorive.R

/**
 * Exercises Rive — a real-time vector-animation runtime — under Berberis
 * ARM64->x86_64 translation. Rive.init loads the arm64-v8a librive-android.so;
 * the probe then parses a bundled .riv document (assets/vehicles.riv) natively
 * via File(bytes) and inspects the resulting artboard (its name + animation /
 * state-machine counts), exercising the native binary-format parser and
 * document builder without a GL surface. Logs "RIVE OK" or "RIVE FAIL" so the
 * suite's StatusTest can assert a clean run. Fully on-device, no Google Play
 * Services.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Loads librive-android.so and sets up the native C++ environment.
            Rive.init(applicationContext)

            val bytes = assets.open(ASSET_NAME).use { it.readBytes() }
            // Native parse of the binary .riv document.
            val file = File(bytes)
            val artboard = file.firstArtboard
            val name = artboard.name
            val animations = artboard.animationCount
            val stateMachines = artboard.stateMachineCount

            // A real .riv must yield a named artboard with at least one
            // animation; zeroed/garbage output means the native parse produced
            // nothing.
            if (name.isNotEmpty() && animations > 0) {
                "RIVE OK (librive parsed artboard=\"$name\", animations=$animations, " +
                    "stateMachines=$stateMachines)"
            } else {
                "RIVE FAIL: empty parse name=\"$name\" animations=$animations " +
                    "stateMachines=$stateMachines"
            }
        } catch (t: Throwable) {
            "RIVE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloRive"
        private const val ASSET_NAME = "vehicles.riv"
    }
}
