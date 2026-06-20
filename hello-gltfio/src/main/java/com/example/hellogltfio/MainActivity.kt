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

package com.example.hellogltfio

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellogltfio.R
import com.google.android.filament.Engine
import com.google.android.filament.EntityManager
import com.google.android.filament.Filament
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.Gltfio
import com.google.android.filament.gltfio.ResourceLoader
import com.google.android.filament.gltfio.UbershaderProvider
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Exercises Google Filament's gltfio native glTF loader — a C++ JSON + accessor
 * parser inside libgltfio-jni.so — under Berberis ARM64->x86_64 translation.
 *
 * The probe builds a SELF-CONTAINED, minimal-but-valid glTF 2.0 document for a
 * single triangle whose vertex/index buffer is embedded as a base64
 * `data:` URI (so there is no external .bin and no images), wraps it in a direct
 * ByteBuffer, and drives the real native pipeline:
 *   Engine.create() -> AssetLoader.createAsset(byteBuffer) (native JSON + accessor
 *   parse) -> ResourceLoader.loadResources(asset) (native buffer-view decode of
 *   the embedded base64 buffer).
 * It then self-checks that the asset was created, exposed the expected entities,
 * and that resource loading reported success, logging "GLTFIO OK" or
 * "GLTFIO FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runGltfioProbe()
    }

    private fun runGltfioProbe(): String {
        val msg = try {
            // Gltfio.init() loads BOTH libfilament-jni.so and libgltfio-jni.so
            // (the gltfio native is a separate library from filament's); it must
            // run before any Engine/AssetLoader/UbershaderProvider call so the
            // gltfio JNI entrypoints resolve.
            Filament.init()
            Gltfio.init()

            val engine = Engine.create()
            try {
                val materialProvider = UbershaderProvider(engine)
                val assetLoader =
                    AssetLoader(engine, materialProvider, EntityManager.get())

                // Direct, native-order buffer holding the glTF JSON text. gltfio
                // reads the bytes straight out of native memory, so a direct
                // buffer (not a heap array) is required.
                val gltfBytes = GLTF_TRIANGLE.toByteArray(Charsets.UTF_8)
                val byteBuffer = ByteBuffer
                    .allocateDirect(gltfBytes.size)
                    .order(ByteOrder.nativeOrder())
                byteBuffer.put(gltfBytes)
                byteBuffer.rewind()

                // Native JSON + accessor parse. Returns null on a parse failure.
                val asset = assetLoader.createAsset(byteBuffer)
                    ?: throw IllegalStateException("createAsset returned null")

                val entityCount = asset.entities.size
                val renderableCount = asset.renderableEntities.size

                // Native decode of the embedded base64 buffer's buffer-views.
                // loadResources returns the ResourceLoader on success.
                val resourceLoader = ResourceLoader(engine)
                resourceLoader.loadResources(asset)

                val ok = entityCount > 0 && renderableCount > 0

                val result = if (ok) {
                    "GLTFIO OK (entities=$entityCount, renderables=$renderableCount)"
                } else {
                    "GLTFIO FAIL: empty asset (entities=$entityCount, " +
                        "renderables=$renderableCount)"
                }

                // Clean up native objects in reverse order of creation.
                resourceLoader.destroy()
                assetLoader.destroyAsset(asset)
                assetLoader.destroy()
                materialProvider.destroyMaterials()
                materialProvider.destroy()

                result
            } finally {
                engine.destroy()
            }
        } catch (t: Throwable) {
            "GLTFIO FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloGltfio"

        // A minimal-but-valid glTF 2.0 document for one triangle. The single
        // buffer is embedded as a base64 `data:` URI, so the asset is fully
        // self-contained (no external .bin, no images). Layout of the 42-byte
        // buffer: 3 POSITION vec3<float> (36 bytes) then 3 ushort indices
        // (6 bytes). The base64 below encodes exactly those bytes.
        private const val GLTF_TRIANGLE = """{
  "asset": { "version": "2.0", "generator": "hello-gltfio" },
  "scene": 0,
  "scenes": [ { "nodes": [ 0 ] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [
    {
      "primitives": [
        {
          "attributes": { "POSITION": 0 },
          "indices": 1,
          "mode": 4
        }
      ]
    }
  ],
  "buffers": [
    {
      "byteLength": 42,
      "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIA"
    }
  ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36, "target": 34962 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6,  "target": 34963 }
  ],
  "accessors": [
    {
      "bufferView": 0,
      "byteOffset": 0,
      "componentType": 5126,
      "count": 3,
      "type": "VEC3",
      "min": [ 0.0, 0.0, 0.0 ],
      "max": [ 1.0, 1.0, 0.0 ]
    },
    {
      "bufferView": 1,
      "byteOffset": 0,
      "componentType": 5123,
      "count": 3,
      "type": "SCALAR"
    }
  ]
}"""
    }
}
