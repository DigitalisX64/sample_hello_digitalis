package com.example.hellomsaa

import android.opengl.GLES30
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.util.Log
import androidx.appcompat.app.AppCompatActivity
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Multisample anti-aliasing (MSAA) sample for the Digitalis suite.
 *
 * The same sharp-edged triangle is rendered four times, each into its own
 * off-screen framebuffer whose colour attachment uses a different multisample
 * count, then resolved and shown in a 2x2 grid:
 *
 *     +-----------+-----------+
 *     |  1x (off) |    2x     |
 *     +-----------+-----------+
 *     |    4x     |    8x     |
 *     +-----------+-----------+
 *
 * Going left-to-right, top-to-bottom the triangle's diagonal edges get
 * progressively smoother as the sample count rises, which is the visible
 * signature of working MSAA. Requested sample counts above GL_MAX_SAMPLES are
 * clamped. The scene is static so the suite's ScreenshotTest output is stable.
 *
 * Under Berberis ARM64->x86_64 translation the guest GLES calls go through the
 * proxy libraries to the host GLES driver. With the ANGLE driver the GLES is
 * translated to Vulkan, where multisampled renderbuffers and glBlitFramebuffer
 * resolves run on the host GPU.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val view = GLSurfaceView(this).apply {
            setEGLContextClientVersion(3)
            // RGBA8 window so the single-sample resolve target matches its format.
            setEGLConfigChooser(8, 8, 8, 8, 0, 0)
            setRenderer(MsaaRenderer())
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        setContentView(view)
    }

    private class MsaaRenderer : GLSurfaceView.Renderer {
        // Sample counts demonstrated, one per grid cell. 1 == no multisampling.
        private val requestedSamples = intArrayOf(1, 2, 4, 8)

        private var width = 0
        private var height = 0
        private var program = 0
        private var vbo = 0

        // Per-level multisample framebuffers + their colour renderbuffers.
        private val msFbo = IntArray(requestedSamples.size)
        private val msColor = IntArray(requestedSamples.size)
        // Shared single-sample target the multisample buffers resolve into.
        private var resolveFbo = 0
        private var resolveColor = 0

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            val maxSamples = intArrayOf(0)
            GLES30.glGetIntegerv(GLES30.GL_MAX_SAMPLES, maxSamples, 0)
            Log.i(
                TAG,
                "GL_VERSION=${GLES30.glGetString(GLES30.GL_VERSION)} " +
                    "GL_MAX_SAMPLES=${maxSamples[0]}"
            )

            program = buildProgram(VERTEX_SHADER, FRAGMENT_SHADER)

            // A triangle whose three edges all sit at shallow, non-axis-aligned
            // slopes so multisampling has visible edges to smooth.
            val verts = floatArrayOf(
                -0.9f, -0.7f,
                0.9f, -0.9f,
                -0.7f, 0.9f
            )
            val buffer = java.nio.ByteBuffer.allocateDirect(verts.size * 4)
                .order(java.nio.ByteOrder.nativeOrder())
                .asFloatBuffer()
            buffer.put(verts).position(0)
            val ids = IntArray(1)
            GLES30.glGenBuffers(1, ids, 0)
            vbo = ids[0]
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo)
            GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, verts.size * 4, buffer, GLES30.GL_STATIC_DRAW)
            GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, 0)
        }

        override fun onSurfaceChanged(gl: GL10?, w: Int, h: Int) {
            width = w
            height = h
            createTargets(w / 2, h / 2)
        }

        /** (Re)create the per-level multisample FBOs and the resolve FBO at cell size. */
        private fun createTargets(cw: Int, ch: Int) {
            deleteTargets()
            if (cw <= 0 || ch <= 0) return

            val maxSamples = intArrayOf(0)
            GLES30.glGetIntegerv(GLES30.GL_MAX_SAMPLES, maxSamples, 0)

            GLES30.glGenFramebuffers(msFbo.size, msFbo, 0)
            GLES30.glGenRenderbuffers(msColor.size, msColor, 0)
            for (i in requestedSamples.indices) {
                val samples = minOf(requestedSamples[i], maxSamples[0])
                GLES30.glBindRenderbuffer(GLES30.GL_RENDERBUFFER, msColor[i])
                if (samples > 1) {
                    GLES30.glRenderbufferStorageMultisample(
                        GLES30.GL_RENDERBUFFER, samples, GLES30.GL_RGBA8, cw, ch
                    )
                } else {
                    // 1x baseline: a plain single-sample renderbuffer (aliased).
                    GLES30.glRenderbufferStorage(GLES30.GL_RENDERBUFFER, GLES30.GL_RGBA8, cw, ch)
                }
                GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, msFbo[i])
                GLES30.glFramebufferRenderbuffer(
                    GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
                    GLES30.GL_RENDERBUFFER, msColor[i]
                )
                val status = GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER)
                if (status != GLES30.GL_FRAMEBUFFER_COMPLETE) {
                    Log.e(TAG, "level $i (samples=$samples) FBO incomplete: 0x%x".format(status))
                }
            }

            val fb = IntArray(1)
            val rb = IntArray(1)
            GLES30.glGenFramebuffers(1, fb, 0)
            GLES30.glGenRenderbuffers(1, rb, 0)
            resolveFbo = fb[0]
            resolveColor = rb[0]
            GLES30.glBindRenderbuffer(GLES30.GL_RENDERBUFFER, resolveColor)
            GLES30.glRenderbufferStorage(GLES30.GL_RENDERBUFFER, GLES30.GL_RGBA8, cw, ch)
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, resolveFbo)
            GLES30.glFramebufferRenderbuffer(
                GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0,
                GLES30.GL_RENDERBUFFER, resolveColor
            )
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        }

        private fun deleteTargets() {
            if (msFbo[0] != 0) GLES30.glDeleteFramebuffers(msFbo.size, msFbo, 0)
            if (msColor[0] != 0) GLES30.glDeleteRenderbuffers(msColor.size, msColor, 0)
            msFbo.fill(0); msColor.fill(0)
            if (resolveFbo != 0) {
                val fb = intArrayOf(resolveFbo); GLES30.glDeleteFramebuffers(1, fb, 0); resolveFbo = 0
            }
            if (resolveColor != 0) {
                val rb = intArrayOf(resolveColor); GLES30.glDeleteRenderbuffers(1, rb, 0); resolveColor = 0
            }
        }

        override fun onDrawFrame(gl: GL10?) {
            val cw = width / 2
            val ch = height / 2
            if (cw <= 0 || ch <= 0 || resolveFbo == 0) return

            // Clear the window so the inter-cell gaps stay a neutral colour.
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
            GLES30.glViewport(0, 0, width, height)
            GLES30.glClearColor(0.02f, 0.02f, 0.04f, 1.0f)
            GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)

            for (i in requestedSamples.indices) {
                // Render the triangle into this level's multisample buffer.
                GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, msFbo[i])
                GLES30.glViewport(0, 0, cw, ch)
                GLES30.glClearColor(0.06f, 0.10f, 0.20f, 1.0f) // dark blue
                GLES30.glClear(GLES30.GL_COLOR_BUFFER_BIT)
                GLES30.glUseProgram(program)
                GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo)
                val posLoc = GLES30.glGetAttribLocation(program, "aPos")
                GLES30.glEnableVertexAttribArray(posLoc)
                GLES30.glVertexAttribPointer(posLoc, 2, GLES30.GL_FLOAT, false, 0, 0)
                GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
                GLES30.glDisableVertexAttribArray(posLoc)
                GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, 0)

                // Resolve multisample -> single-sample (same format and size).
                GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, msFbo[i])
                GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, resolveFbo)
                GLES30.glBlitFramebuffer(
                    0, 0, cw, ch, 0, 0, cw, ch,
                    GLES30.GL_COLOR_BUFFER_BIT, GLES30.GL_NEAREST
                )

                // Copy the resolved cell into its grid quadrant on the window.
                // Grid (GL origin bottom-left): index 0 top-left, 1 top-right,
                // 2 bottom-left, 3 bottom-right.
                val col = i % 2
                val row = i / 2
                val dx0 = col * cw
                val dy0 = if (row == 0) ch else 0
                GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, resolveFbo)
                GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, 0)
                GLES30.glBlitFramebuffer(
                    0, 0, cw, ch, dx0, dy0, dx0 + cw, dy0 + ch,
                    GLES30.GL_COLOR_BUFFER_BIT, GLES30.GL_NEAREST
                )
            }
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        }

        private fun buildProgram(vsrc: String, fsrc: String): Int {
            val vs = compile(GLES30.GL_VERTEX_SHADER, vsrc)
            val fs = compile(GLES30.GL_FRAGMENT_SHADER, fsrc)
            val prog = GLES30.glCreateProgram()
            GLES30.glAttachShader(prog, vs)
            GLES30.glAttachShader(prog, fs)
            GLES30.glLinkProgram(prog)
            val linked = IntArray(1)
            GLES30.glGetProgramiv(prog, GLES30.GL_LINK_STATUS, linked, 0)
            if (linked[0] == 0) {
                Log.e(TAG, "link failed: ${GLES30.glGetProgramInfoLog(prog)}")
            }
            GLES30.glDeleteShader(vs)
            GLES30.glDeleteShader(fs)
            return prog
        }

        private fun compile(type: Int, src: String): Int {
            val shader = GLES30.glCreateShader(type)
            GLES30.glShaderSource(shader, src)
            GLES30.glCompileShader(shader)
            val ok = IntArray(1)
            GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, ok, 0)
            if (ok[0] == 0) {
                Log.e(TAG, "compile failed: ${GLES30.glGetShaderInfoLog(shader)}")
            }
            return shader
        }
    }

    companion object {
        private const val TAG = "HelloMSAA"

        private const val VERTEX_SHADER = """#version 300 es
in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
"""

        private const val FRAGMENT_SHADER = """#version 300 es
precision mediump float;
out vec4 fragColor;
void main() {
    fragColor = vec4(0.95, 0.55, 0.10, 1.0); // orange
}
"""
    }
}
