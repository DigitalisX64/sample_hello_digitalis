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

package com.example.hellofilamentrender

import android.app.Activity
import android.os.Bundle
import android.util.Log
import android.view.Choreographer
import android.view.Surface
import android.view.SurfaceView
import android.view.ViewGroup
import com.example.hellodigitalis.hellofilamentrender.R
import com.google.android.filament.Camera
import com.google.android.filament.Engine
import com.google.android.filament.EntityManager
import com.google.android.filament.Filament
import com.google.android.filament.LightManager
import com.google.android.filament.Renderer
import com.google.android.filament.Scene
import com.google.android.filament.Skybox
import com.google.android.filament.SwapChain
import com.google.android.filament.View
import com.google.android.filament.Viewport
import com.google.android.filament.android.UiHelper
import com.google.android.filament.gltfio.AssetLoader
import com.google.android.filament.gltfio.FilamentAsset
import com.google.android.filament.gltfio.Gltfio
import com.google.android.filament.gltfio.ResourceLoader
import com.google.android.filament.gltfio.UbershaderProvider
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Drives Google Filament's native render pipeline to an on-screen SurfaceView
 * under Berberis ARM64->x86_64 translation, rendering a real glTF model. The
 * renderer's work — the GLES/Vulkan backend command stream plus a large amount of
 * NEON-heavy SIMD math — runs inside the arm64-v8a libfilament-jni.so, and the
 * model is parsed and shaded by libgltfio-jni.so, so a clean frame proves both
 * native render paths translate correctly.
 *
 * Scene: a self-contained glTF 2.0 cube (positions + per-face normals + a
 * pbrMetallicRoughness material, embedded as a base64 buffer — no external assets)
 * loaded with gltfio's AssetLoader/ResourceLoader and shaded by its ubershader
 * MaterialProvider (so no offline-compiled .filamat is needed). It is lit by one
 * fixed directional light and viewed by a fixed camera at an angle that shows
 * three faces, over a solid sky-blue Skybox background. Nothing animates and the
 * camera/light/material are constant, so every frame is byte-identical — exactly
 * what the suite's ScreenshotTest needs.
 *
 * This sample is enabled by the guest libgui.so stub (see
 * frameworks/libs/binary_translation/android_api/digitalis_libgui_stub): Filament's
 * Android platform layer dlopen()s libgui.so on the SwapChain-present path, which
 * Digitalis now satisfies so the present path no longer faults.
 */
class MainActivity : Activity() {

    private lateinit var surfaceView: SurfaceView
    private lateinit var uiHelper: UiHelper
    private val choreographer: Choreographer = Choreographer.getInstance()

    private lateinit var engine: Engine
    private lateinit var renderer: Renderer
    private lateinit var scene: Scene
    private lateinit var view: View
    private lateinit var camera: Camera
    private var cameraEntity: Int = 0
    private lateinit var skybox: Skybox

    private var lightEntity: Int = 0
    private lateinit var materialProvider: UbershaderProvider
    private lateinit var assetLoader: AssetLoader
    private lateinit var resourceLoader: ResourceLoader
    private var asset: FilamentAsset? = null

    private var swapChain: SwapChain? = null

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            choreographer.postFrameCallback(this)
            val sc = swapChain ?: return
            if (renderer.beginFrame(sc, frameTimeNanos)) {
                renderer.render(view)
                renderer.endFrame()
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        surfaceView = SurfaceView(this).apply {
            layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        }
        setContentView(surfaceView)

        setupFilament()
        setupScene()

        // ContextErrorPolicy.DONT_CHECK: don't probe the EGL/Vulkan context for
        // errors during attach — the host-backed swapchain under translation
        // doesn't need that liveness check and it can spuriously fail.
        uiHelper = UiHelper(UiHelper.ContextErrorPolicy.DONT_CHECK)
        uiHelper.setRenderCallback(SurfaceCallback())
        uiHelper.attachTo(surfaceView)
    }

    private fun setupFilament() {
        // Gltfio.init() loads BOTH libfilament-jni.so and libgltfio-jni.so.
        Filament.init()
        Gltfio.init()
        // Vulkan backend: under Digitalis the well-supported GPU path is Vulkan
        // (guest libvulkan -> host GFXStream VkDecoder), as in hello-vulkan.
        engine = Engine.create(Engine.Backend.VULKAN)
        renderer = engine.createRenderer()
        scene = engine.createScene()
        view = engine.createView()

        cameraEntity = EntityManager.get().create()
        camera = engine.createCamera(cameraEntity)
        // Physically-based exposure (the "sunny 16" rule) so the daylight-intensity
        // directional light below yields a properly-exposed, deterministic frame.
        camera.setExposure(16.0f, 1.0f / 125.0f, 100.0f)
    }

    private fun setupScene() {
        // Solid sky-blue background.
        skybox = Skybox.Builder()
            .color(CLEAR_R, CLEAR_G, CLEAR_B, 1.0f)
            .build(engine)
        scene.skybox = skybox

        view.scene = scene
        view.camera = camera

        // Load and shade the embedded glTF cube with gltfio.
        materialProvider = UbershaderProvider(engine)
        assetLoader = AssetLoader(engine, materialProvider, EntityManager.get())
        val gltfBytes = GLTF_CUBE.toByteArray(Charsets.UTF_8)
        val byteBuffer = ByteBuffer
            .allocateDirect(gltfBytes.size)
            .order(ByteOrder.nativeOrder())
        byteBuffer.put(gltfBytes)
        byteBuffer.rewind()
        val loaded = assetLoader.createAsset(byteBuffer)
            ?: error("gltfio createAsset returned null")
        asset = loaded
        resourceLoader = ResourceLoader(engine)
        resourceLoader.loadResources(loaded)
        scene.addEntities(loaded.entities)

        // One fixed directional light (a daylight sun) so the cube's three visible
        // faces are shaded distinctly, giving the frame a clear 3D form.
        lightEntity = EntityManager.get().create()
        LightManager.Builder(LightManager.Type.DIRECTIONAL)
            .color(1.0f, 0.98f, 0.95f)
            .intensity(100_000.0f)
            .direction(-0.6f, -1.0f, -0.5f)
            .castShadows(false)
            .build(engine, lightEntity)
        scene.addEntity(lightEntity)

        // Fixed camera looking at the unit cube from an angle that reveals three
        // faces. eye / center / up.
        camera.lookAt(
            1.9, 1.5, 2.3,
            0.0, 0.0, 0.0,
            0.0, 1.0, 0.0
        )

        Log.i(
            TAG,
            "FILAMENT setup: backend=${engine.backend} model=glTF-cube " +
                "renderables=${loaded.renderableEntities.size}"
        )
    }

    private inner class SurfaceCallback : UiHelper.RendererCallback {
        override fun onNativeWindowChanged(surface: Surface) {
            swapChain?.let { engine.destroySwapChain(it) }
            swapChain = engine.createSwapChain(surface)
        }

        override fun onDetachedFromSurface() {
            swapChain?.let {
                engine.destroySwapChain(it)
                engine.flushAndWait()
                swapChain = null
            }
        }

        override fun onResized(width: Int, height: Int) {
            view.viewport = Viewport(0, 0, width, height)
            val aspect = width.toDouble() / height.toDouble()
            camera.setProjection(45.0, aspect, 0.1, 100.0, Camera.Fov.VERTICAL)
        }
    }

    override fun onResume() {
        super.onResume()
        choreographer.postFrameCallback(frameCallback)
    }

    override fun onPause() {
        super.onPause()
        choreographer.removeFrameCallback(frameCallback)
    }

    override fun onDestroy() {
        super.onDestroy()
        choreographer.removeFrameCallback(frameCallback)
        uiHelper.detach()

        swapChain?.let { engine.destroySwapChain(it) }
        swapChain = null

        asset?.let { assetLoader.destroyAsset(it) }
        asset = null
        resourceLoader.destroy()
        assetLoader.destroy()
        materialProvider.destroyMaterials()
        materialProvider.destroy()

        engine.destroyEntity(lightEntity)
        EntityManager.get().destroy(lightEntity)

        engine.destroyRenderer(renderer)
        engine.destroyView(view)
        engine.destroyScene(scene)
        engine.destroySkybox(skybox)
        engine.destroyCameraComponent(cameraEntity)
        EntityManager.get().destroy(cameraEntity)

        engine.destroy()
    }

    companion object {
        private const val TAG = "HelloFilamentRender"

        // Fixed sky-blue background color.
        private const val CLEAR_R = 0.10f
        private const val CLEAR_G = 0.40f
        private const val CLEAR_B = 0.85f

        // A self-contained glTF 2.0 unit cube: 24 vertices (POSITION + per-face
        // NORMAL), 36 indices, and one orange pbrMetallicRoughness material; the
        // single buffer is embedded as a base64 data: URI (no external .bin/images).
        private const val GLTF_CUBE =
            """{"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}],"materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.9,0.45,0.1,1.0],"metallicFactor":0.0,"roughnessFactor":0.6}}],"buffers":[{"byteLength":648,"uri":"data:application/octet-stream;base64,AAAAPwAAAL8AAAA/AAAAPwAAAL8AAAC/AAAAPwAAAD8AAAC/AAAAPwAAAD8AAAA/AAAAvwAAAL8AAAC/AAAAvwAAAL8AAAA/AAAAvwAAAD8AAAA/AAAAvwAAAD8AAAC/AAAAvwAAAD8AAAA/AAAAPwAAAD8AAAA/AAAAPwAAAD8AAAC/AAAAvwAAAD8AAAC/AAAAvwAAAL8AAAC/AAAAPwAAAL8AAAC/AAAAPwAAAL8AAAA/AAAAvwAAAL8AAAA/AAAAvwAAAL8AAAA/AAAAPwAAAL8AAAA/AAAAPwAAAD8AAAA/AAAAvwAAAD8AAAA/AAAAPwAAAL8AAAC/AAAAvwAAAL8AAAC/AAAAvwAAAD8AAAC/AAAAPwAAAD8AAAC/AACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAvwAAAAAAAAAAAACAvwAAAAAAAAAAAACAvwAAAAAAAAAAAACAvwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgD8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAgL8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIC/AAABAAIAAAACAAMABAAFAAYABAAGAAcACAAJAAoACAAKAAsADAANAA4ADAAOAA8AEAARABIAEAASABMAFAAVABYAFAAWABcA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":288,"target":34962},{"buffer":0,"byteOffset":288,"byteLength":288,"target":34962},{"buffer":0,"byteOffset":576,"byteLength":72,"target":34963}],"accessors":[{"bufferView":0,"componentType":5126,"count":24,"type":"VEC3","min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},{"bufferView":1,"componentType":5126,"count":24,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":36,"type":"SCALAR"}]}"""
    }
}
