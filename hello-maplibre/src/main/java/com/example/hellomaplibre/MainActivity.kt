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
package com.example.hellomaplibre

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellomaplibre.R
import org.maplibre.android.MapLibre
import org.maplibre.android.offline.OfflineManager
import org.maplibre.android.storage.FileSource

/**
 * Exercises the MapLibre Native GL Android SDK — a maps rendering engine whose
 * core is C++ shipped as arm64-v8a libmaplibre.so — under Berberis
 * ARM64->x86_64 translation, headlessly (no GL surface, no network).
 *
 * MapLibre.getInstance(context) loads libmaplibre.so and runs its real native
 * initialization (the native TileServerOptions config + the C++ DefaultFileSource
 * peer). The probe then drives further JNI-backed, non-GL paths and self-checks
 * their return values:
 *   - FileSource.getApiBaseUrl(): a native round-trip that must return a
 *     non-null base URL string.
 *   - FileSource.isActivated()/activate()/deactivate(): native state that must
 *     flip false -> true -> false.
 *   - OfflineManager.getInstance(context): native init that opens/creates the
 *     mbgl-cache.db SQLite database on disk, followed by a synchronous native
 *     setOfflineMapboxTileCountLimit() call.
 *
 * Full map rendering needs a GLSurfaceView + a style + network tiles, which is
 * not feasible headlessly; this is a native-init + native-API smoke test that
 * still reaches real libmaplibre.so code, not just System.loadLibrary.
 *
 * Logs "MAPLIBRE OK" on success or "MAPLIBRE FAIL" on failure so the suite's
 * StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Loads arm64-v8a libmaplibre.so and runs native init (no key, no
            // network, no GL surface). The single-arg overload sets a null API
            // key and never validates it, so no token is required.
            MapLibre.getInstance(applicationContext)

            // Native round-trip: the C++ DefaultFileSource peer returns its
            // configured base URL. A working native must hand back a non-null,
            // non-empty string.
            val fileSource = FileSource.getInstance(applicationContext)
            val baseUrl: String? = fileSource.apiBaseUrl

            // Native activation state must flip false -> true -> false.
            val before = fileSource.isActivated
            fileSource.activate()
            val activated = fileSource.isActivated
            fileSource.deactivate()
            val after = fileSource.isActivated

            // Native init that opens/creates the mbgl-cache.db SQLite database,
            // followed by a synchronous native (external) setter call.
            val offlineManager = OfflineManager.getInstance(applicationContext)
            offlineManager.setOfflineMapboxTileCountLimit(6000L)

            val baseUrlOk = !baseUrl.isNullOrEmpty()
            val activationOk = !before && activated && !after

            if (baseUrlOk && activationOk) {
                "MAPLIBRE OK (native init; baseUrl=$baseUrl; " +
                    "activate flip ok; offline cache + tile-limit set)"
            } else {
                "MAPLIBRE FAIL: baseUrlOk=$baseUrlOk (baseUrl=$baseUrl) " +
                    "activationOk=$activationOk " +
                    "(before=$before activated=$activated after=$after)"
            }
        } catch (t: Throwable) {
            "MAPLIBRE FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloMaplibre"
    }
}
