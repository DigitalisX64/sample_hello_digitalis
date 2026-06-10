package com.example.hellocameracore

import android.graphics.Bitmap
import android.graphics.Color
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellocameracore.R
import java.io.File
import java.nio.ByteBuffer

/**
 * Exercises androidx.camera:camera-core's native code
 * (jni/arm64-v8a/libimage_processing_util_jni.so) under Berberis
 * ARM64->x86_64 translation.
 *
 * camera-core's native conversions (YUV<->RGB, rotation, JPEG-to-Surface)
 * normally take an ImageProxy/Surface from a live camera and aren't reachable
 * headlessly. The two exceptions are the public static helpers
 * ImageProcessingUtil.copyBitmapToByteBuffer / copyByteBufferToBitmap, which
 * wrap the native nativeCopyBetweenByteBufferAndBitmap entry point and need only
 * a Bitmap + a direct ByteBuffer — no camera. The probe:
 *   1. forces ImageProcessingUtil's static init (System.loadLibrary
 *      "image_processing_util_jni") so the arm64 .so is dlopened and its
 *      JNI_OnLoad runs under translation, then confirms the .so is mapped via
 *      /proc/self/maps;
 *   2. builds a known ARGB_8888 Bitmap, copies its pixels into a direct
 *      ByteBuffer via the native copyBitmapToByteBuffer, copies that buffer back
 *      into a fresh Bitmap via the native copyByteBufferToBitmap, and asserts a
 *      known sentinel pixel survived the native round-trip.
 *
 * The native code uses the NDK jnigraphics API (AndroidBitmap_lockPixels) and a
 * direct-buffer pointer, so a clean round-trip means real arm64 native pixel
 * copying executed correctly under translation. Logs "CAMERACORE OK" / a
 * diagnostic so the suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runCameraCoreProbe()
    }

    private fun runCameraCoreProbe(): String {
        val msg = try {
            // The native helpers live on a @RestrictTo(LIBRARY_GROUP) class;
            // reach them reflectively so the cross-module lint never gates the
            // build. Loading the class triggers its <clinit>, which calls
            // System.loadLibrary("image_processing_util_jni").
            val cls = Class.forName("androidx.camera.core.ImageProcessingUtil")

            val libMapped = isLibMapped(LIB_NAME)
            if (!libMapped) {
                throw IllegalStateException("$LIB_NAME not in /proc/self/maps after class init")
            }

            val copyBitmapToByteBuffer = cls.getDeclaredMethod(
                "copyBitmapToByteBuffer",
                Bitmap::class.java, ByteBuffer::class.java, Int::class.javaPrimitiveType
            ).apply { isAccessible = true }
            val copyByteBufferToBitmap = cls.getDeclaredMethod(
                "copyByteBufferToBitmap",
                Bitmap::class.java, ByteBuffer::class.java, Int::class.javaPrimitiveType
            ).apply { isAccessible = true }

            val width = 8
            val height = 4
            val sentinel = Color.argb(0xAB, 0x12, 0x34, 0x56)

            // Source bitmap painted with a distinct value per pixel, plus a
            // known sentinel at (3,2) we assert survives the native round-trip.
            val src = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            for (y in 0 until height) {
                for (x in 0 until width) {
                    src.setPixel(x, y, Color.argb(0xFF, x * 8, y * 8, (x + y) * 4))
                }
            }
            src.setPixel(3, 2, sentinel)

            val rowBytes = src.rowBytes
            // Direct buffer: the native side reads it via GetDirectBufferAddress.
            val buffer = ByteBuffer.allocateDirect(rowBytes * height)

            // Bitmap -> direct ByteBuffer (native arm64).
            copyBitmapToByteBuffer.invoke(null, src, buffer, rowBytes)

            // direct ByteBuffer -> fresh Bitmap (native arm64).
            val dst = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            buffer.rewind()
            copyByteBufferToBitmap.invoke(null, dst, buffer, rowBytes)

            val gotSentinel = dst.getPixel(3, 2)
            val corner = dst.getPixel(width - 1, height - 1)
            val expectedCorner = src.getPixel(width - 1, height - 1)

            val sentinelOk = gotSentinel == sentinel
            val cornerOk = corner == expectedCorner

            if (sentinelOk && cornerOk) {
                "CAMERACORE OK ($LIB_NAME mapped + native Bitmap<->ByteBuffer " +
                    "round-trip ${width}x$height ARGB_8888; sentinel " +
                    "0x${Integer.toHexString(sentinel)} preserved)"
            } else {
                "CAMERACORE MISMATCH: sentinelOk=$sentinelOk " +
                    "got=0x${Integer.toHexString(gotSentinel)} " +
                    "want=0x${Integer.toHexString(sentinel)} cornerOk=$cornerOk"
            }
        } catch (t: Throwable) {
            "CAMERACORE ERROR: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    private fun isLibMapped(name: String): Boolean = try {
        File("/proc/self/maps").bufferedReader().useLines { lines ->
            lines.any { it.contains(name) }
        }
    } catch (t: Throwable) {
        false
    }

    companion object {
        private const val TAG = "HelloCameraCore"
        private const val LIB_NAME = "libimage_processing_util_jni.so"
    }
}
