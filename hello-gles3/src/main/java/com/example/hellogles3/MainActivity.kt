package com.example.hellogles3

import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import javax.microedition.khronos.egl.EGL10
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.egl.EGLContext
import javax.microedition.khronos.egl.EGLDisplay
import javax.microedition.khronos.opengles.GL10

/**
 * Minimal OpenGL ES 3.2 EGL repro for the Digitalis sample suite.
 *
 * The activity creates a GLSurfaceView whose EGL context is requested
 * explicitly at major=3, minor=2 (OpenGL ES 3.2) via a custom
 * EGLContextFactory. Under Berberis ARM64->x86_64 translation the guest
 * EGL/GLES calls are forwarded to the host through the proxy libraries and
 * the gfxstream guest EGL implementation. A 3.2 context request used to be
 * rejected with EGL_BAD_CONFIG (the host GLES translator caps at ES 3.1),
 * leaving the surface black; the gfxstream guest-EGL clamp makes the request
 * succeed by lowering the granted minor version to what the host supports.
 *
 * The renderer draws a deterministic two-colour pattern (dark-blue clear with
 * a centred orange rectangle via glScissor, no rasterisation/AA) so the
 * suite's ScreenshotTest can validate the rendered output pixel-for-pixel.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val view = GLSurfaceView(this).apply {
            setEGLConfigChooser(8, 8, 8, 0, 0, 0)
            setEGLContextFactory(Es32ContextFactory())
            setRenderer(Renderer())
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        setContentView(view)
    }

    /** Requests an OpenGL ES 3.2 context, falling back to ES 3.0 if rejected. */
    private class Es32ContextFactory : GLSurfaceView.EGLContextFactory {
        override fun createContext(
            egl: EGL10,
            display: EGLDisplay,
            config: EGLConfig
        ): EGLContext {
            val attribs = intArrayOf(
                EGL_CONTEXT_MAJOR_VERSION, 3,
                EGL_CONTEXT_MINOR_VERSION, 2,
                EGL10.EGL_NONE
            )
            var context = egl.eglCreateContext(display, config, EGL10.EGL_NO_CONTEXT, attribs)
            if (context == null || context === EGL10.EGL_NO_CONTEXT) {
                Log.w(
                    TAG,
                    "ES 3.2 context request rejected (eglError=0x%x); falling back to ES 3.0"
                        .format(egl.eglGetError())
                )
                val fallback = intArrayOf(EGL_CONTEXT_MAJOR_VERSION, 3, EGL10.EGL_NONE)
                context = egl.eglCreateContext(display, config, EGL10.EGL_NO_CONTEXT, fallback)
            }
            return context
        }

        override fun destroyContext(egl: EGL10, display: EGLDisplay, context: EGLContext) {
            egl.eglDestroyContext(display, context)
        }
    }

    private class Renderer : GLSurfaceView.Renderer {
        private var width = 0
        private var height = 0

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            Log.i(
                TAG,
                "GL_VERSION=${GLES30.glGetString(GLES30.GL_VERSION)} " +
                    "GLSL=${GLES30.glGetString(GLES30.GL_SHADING_LANGUAGE_VERSION)}"
            )
        }

        override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
            width = w
            height = h
            GLES30.glViewport(0, 0, w, h)
        }

        override fun onDrawFrame(gl: GL10?) {
            GLES30.glDisable(GLES30.GL_SCISSOR_TEST)
            GLES30.glClearColor(0.06f, 0.10f, 0.20f, 1.0f) // dark blue background
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)

            // Centred rectangle (half width/height) painted via scissor clear.
            // Solid colours and axis-aligned edges keep the output deterministic.
            val rw = width / 2
            val rh = height / 2
            if (rw > 0 && rh > 0) {
                GLES30.glEnable(GLES30.GL_SCISSOR_TEST)
                GLES30.glScissor((width - rw) / 2, (height - rh) / 2, rw, rh)
                GLES30.glClearColor(0.95f, 0.55f, 0.10f, 1.0f) // orange
                GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
            }
        }
    }

    companion object {
        private const val TAG = "HelloGLES3"

        // EGL 1.5 / EGL_KHR_create_context context attributes. EGL_CONTEXT_MAJOR_VERSION
        // shares the value of EGL10.EGL_CONTEXT_CLIENT_VERSION (0x3098).
        private const val EGL_CONTEXT_MAJOR_VERSION = 0x3098
        private const val EGL_CONTEXT_MINOR_VERSION = 0x30FB
    }
}
