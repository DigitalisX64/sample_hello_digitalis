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

package com.example.hellofilament

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellofilament.R
import com.google.android.filament.Camera
import com.google.android.filament.Engine
import com.google.android.filament.EntityManager
import com.google.android.filament.Filament
import com.google.android.filament.IndexBuffer
import com.google.android.filament.Renderer
import com.google.android.filament.Scene
import com.google.android.filament.VertexBuffer
import com.google.android.filament.View
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Exercises Google Filament — a native (C++) real-time PBR rendering engine —
 * under Berberis ARM64->x86_64 translation, headlessly (no on-screen surface).
 * Filament.init() loads the arm64-v8a libfilament-jni.so and its native engine;
 * Engine.create() spins up the renderer backend. The probe then allocates the
 * native GPU-resource descriptors that do NOT require an EGL/Vulkan SwapChain:
 * an entity, a 3-vertex VertexBuffer (filled with float position data through a
 * direct ByteBuffer), a 3-index IndexBuffer, a TransformManager component, plus
 * a Camera, Scene, View and Renderer. Each created object is verified non-null
 * (and validated through the engine's isValid* checks where available) before
 * everything is destroyed and the engine torn down, logging "FILAMENT OK" or
 * "FILAMENT FAIL" so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runFilamentProbe()
    }

    private fun runFilamentProbe(): String {
        val msg = try {
            // Loads the native engine library (arm64-v8a libfilament-jni.so).
            Filament.init()

            val engine = Engine.create()
            check(engine.isValid) { "Engine.create() returned an invalid engine" }

            // An entity is a plain int handle minted by the native EntityManager.
            val entity = EntityManager.get().create()
            check(entity != 0) { "EntityManager.create() returned the null entity" }

            // A triangle's three positions, packed into a direct (native-backed)
            // ByteBuffer so Filament's native VertexBuffer can read them without
            // a SwapChain. 3 vertices * 3 floats * 4 bytes.
            val vertexData = ByteBuffer
                .allocateDirect(3 * 3 * 4)
                .order(ByteOrder.nativeOrder())
            for (xyz in TRIANGLE_POSITIONS) vertexData.putFloat(xyz)
            vertexData.flip()

            val vertexBuffer = VertexBuffer.Builder()
                .vertexCount(3)
                .bufferCount(1)
                .attribute(
                    VertexBuffer.VertexAttribute.POSITION,
                    0,
                    VertexBuffer.AttributeType.FLOAT3,
                    0,
                    3 * 4,
                )
                .build(engine)
            check(engine.isValidVertexBuffer(vertexBuffer)) {
                "VertexBuffer.build() produced an invalid buffer"
            }
            vertexBuffer.setBufferAt(engine, 0, vertexData)

            // Three indices into the triangle, in a direct short buffer.
            val indexData = ByteBuffer
                .allocateDirect(3 * 2)
                .order(ByteOrder.nativeOrder())
            for (i in shortArrayOf(0, 1, 2)) indexData.putShort(i)
            indexData.flip()

            val indexBuffer = IndexBuffer.Builder()
                .indexCount(3)
                .bufferType(IndexBuffer.Builder.IndexType.USHORT)
                .build(engine)
            check(engine.isValidIndexBuffer(indexBuffer)) {
                "IndexBuffer.build() produced an invalid buffer"
            }
            indexBuffer.setBuffer(engine, indexData)

            // Register a transform component on the entity via the native
            // TransformManager, then read it back.
            val transformManager = engine.transformManager
            transformManager.create(entity)
            check(transformManager.hasComponent(entity)) {
                "TransformManager did not register a component for the entity"
            }

            // Camera is built on its own entity; Scene/View/Renderer are the
            // remaining native objects that don't need a presentation surface.
            val cameraEntity = EntityManager.get().create()
            val camera: Camera = engine.createCamera(cameraEntity)
            check(camera.entity != 0) { "createCamera() produced an invalid camera" }

            val scene: Scene = engine.createScene()
            check(engine.isValidScene(scene)) { "createScene() produced an invalid scene" }
            scene.addEntity(entity)
            check(scene.entityCount == 1) {
                "Scene did not retain the entity (count=${scene.entityCount})"
            }

            val view: View = engine.createView()
            check(engine.isValidView(view)) { "createView() produced an invalid view" }
            view.scene = scene
            view.camera = camera

            val renderer: Renderer = engine.createRenderer()
            check(engine.isValidRenderer(renderer)) {
                "createRenderer() produced an invalid renderer"
            }

            // Tear everything down in roughly reverse order.
            engine.destroyRenderer(renderer)
            engine.destroyView(view)
            engine.destroyScene(scene)
            engine.destroyCameraComponent(cameraEntity)
            transformManager.destroy(entity)
            engine.destroyIndexBuffer(indexBuffer)
            engine.destroyVertexBuffer(vertexBuffer)
            engine.destroyEntity(cameraEntity)
            engine.destroyEntity(entity)
            EntityManager.get().destroy(cameraEntity)
            EntityManager.get().destroy(entity)
            engine.destroy()

            "FILAMENT OK (engine + vertexbuffer + indexbuffer + scene created/destroyed)"
        } catch (t: Throwable) {
            "FILAMENT FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloFilament"

        // A unit triangle in clip space: three XYZ positions.
        private val TRIANGLE_POSITIONS = floatArrayOf(
            0.0f, 0.5f, 0.0f,
            -0.5f, -0.5f, 0.0f,
            0.5f, -0.5f, 0.0f,
        )
    }
}
