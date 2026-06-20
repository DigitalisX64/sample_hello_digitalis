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
import com.google.android.filament.Renderer
import com.google.android.filament.Scene
import com.google.android.filament.Skybox
import com.google.android.filament.SwapChain
import com.google.android.filament.View
import com.google.android.filament.Viewport
import com.google.android.filament.android.UiHelper

/**
 * Drives Google Filament's native render pipeline to an on-screen SurfaceView
 * under Berberis ARM64->x86_64 translation. Filament's engine is a real-time
 * physically based renderer whose work — the GLES/Vulkan backend command
 * stream plus a large amount of NEON-heavy SIMD math — runs entirely inside the
 * arm64-v8a libfilament-jni.so, so a clean frame proves that native render path
 * translates correctly.
 *
 * To stay free of any compiled .filamat material, the scene's only content is a
 * solid-color [Skybox] used as the clear color. A skybox is rendered by the
 * engine's full begin/render/end frame path (it is not a glClear shortcut), so
 * this still exercises Filament's swap-chain acquisition, view/camera setup, and
 * frame submission — it just yields a deterministic, perfectly uniform frame.
 *
 * KNOWN GAP (not registered in the suite): Filament's render-backend DRIVER
 * SIGSEGVs under translation as soon as it drives a real SwapChain — both the
 * OpenGL backend (crashes on the driver thread after engine init) and the Vulkan
 * backend (crashes during Engine.create). Filament's native engine itself works
 * (see the registered hello-filament headless smoke); only this on-surface render
 * path is unsupported so far. This module is kept on disk so it becomes a live
 * screenshot sample once the translator handles Filament's backend render path.
 * Because the color never changes and nothing animates, every frame is
 * byte-identical, which is exactly what the suite's ScreenshotTest needs.
 *
 * Render flow per frame (Choreographer-driven):
 *   if (renderer.beginFrame(swapChain, frameTimeNanos)) {
 *       renderer.render(view); renderer.endFrame()
 *   }
 * The SwapChain is created the moment UiHelper hands us the native Surface.
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

    private var swapChain: SwapChain? = null

    private val frameCallback = object : Choreographer.FrameCallback {
        override fun doFrame(frameTimeNanos: Long) {
            // Re-arm immediately so the loop keeps running; the screenshot is
            // taken once the (constant) frame is on screen.
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
        setupView()

        // ContextErrorPolicy.DONT_CHECK: don't probe the EGL/Vulkan context for
        // errors during attach — the host-backed swapchain under translation
        // doesn't need that liveness check and it can spuriously fail.
        uiHelper = UiHelper(UiHelper.ContextErrorPolicy.DONT_CHECK)
        uiHelper.setRenderCallback(SurfaceCallback())
        uiHelper.attachTo(surfaceView)
    }

    private fun setupFilament() {
        // Loads the native libfilament-jni.so (arm64-v8a) — the first real call
        // into Filament's translated native code.
        Filament.init()
        // Use Filament's Vulkan backend: under Digitalis the well-supported GPU
        // path is Vulkan (guest libvulkan -> host GFXStream VkDecoder), as in
        // hello-vulkan; the OpenGL backend's GLES-through-proxy render path is not
        // reliable here.
        engine = Engine.create(Engine.Backend.VULKAN)
        renderer = engine.createRenderer()
        scene = engine.createScene()
        view = engine.createView()

        // A Camera in Filament is a component attached to an entity.
        cameraEntity = EntityManager.get().create()
        camera = engine.createCamera(cameraEntity)
    }

    private fun setupView() {
        // The whole frame is a single solid color, supplied by a Skybox so we
        // never need a compiled material. FIXED, distinctive color — this is the
        // value the reference screenshot captures.
        skybox = Skybox.Builder()
            .color(CLEAR_R, CLEAR_G, CLEAR_B, 1.0f)
            .build(engine)
        scene.skybox = skybox

        view.scene = scene
        view.camera = camera
        // Disable post-processing so the on-screen color is exactly the skybox
        // color with no tone-mapping/dithering, keeping the frame deterministic.
        view.isPostProcessingEnabled = false

        Log.i(
            TAG,
            "FILAMENT setup: backend=${engine.backend} " +
                "clear=($CLEAR_R, $CLEAR_G, $CLEAR_B)"
        )
    }

    private inner class SurfaceCallback : UiHelper.RendererCallback {
        override fun onNativeWindowChanged(surface: Surface) {
            // Re-create the swap chain whenever the native window changes.
            swapChain?.let { engine.destroySwapChain(it) }
            swapChain = engine.createSwapChain(surface)
        }

        override fun onDetachedFromSurface() {
            swapChain?.let {
                engine.destroySwapChain(it)
                // Flush so the GPU is done with the destroyed swap chain before
                // anything else touches it.
                engine.flushAndWait()
                swapChain = null
            }
        }

        override fun onResized(width: Int, height: Int) {
            view.viewport = Viewport(0, 0, width, height)
            // A simple symmetric perspective projection; the skybox fills the
            // whole frame regardless of these values, but the camera still needs
            // a valid projection for the frame to render.
            val aspect = width.toDouble() / height.toDouble()
            camera.setProjection(
                45.0, aspect, 0.1, 100.0, Camera.Fov.VERTICAL
            )
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
        // Stop the render loop and detach from the surface first.
        choreographer.removeFrameCallback(frameCallback)
        uiHelper.detach()

        swapChain?.let { engine.destroySwapChain(it) }
        swapChain = null

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

        // Fixed, distinctive clear color (a deep sky blue). Every rendered frame
        // is exactly this color, so the screenshot is deterministic.
        private const val CLEAR_R = 0.10f
        private const val CLEAR_G = 0.40f
        private const val CLEAR_B = 0.85f
    }
}
