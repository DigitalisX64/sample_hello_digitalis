/*
 * Copyright 2013 The Android Open Source Project
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

package com.sample.helper

import android.annotation.TargetApi
import android.app.NativeActivity
import android.content.Context
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager.NameNotFoundException
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.AudioManager
import android.media.AudioTrack
import android.opengl.GLUtils
import android.os.Build
import android.util.Log
import java.io.File
import java.io.FileInputStream
import java.nio.ByteBuffer
import javax.microedition.khronos.opengles.GL10

@TargetApi(Build.VERSION_CODES.GINGERBREAD)
class NDKHelper(var activity: NativeActivity) {

    fun loadLibrary(soname: String) {
        if (soname.isNotEmpty()) {
            System.loadLibrary(soname)
            loadedSO = true
        }
    }

    inner class TextureInformation {
        var ret: Boolean = false
        var alphaChannel: Boolean = false
        var originalWidth: Int = 0
        var originalHeight: Int = 0
        var image: Any? = null
    }

    private fun nextPOT(i: Int): Int {
        var pot = 1
        while (pot < i)
            pot = pot shl 1
        return pot
    }

    private fun scaleBitmap(
        bitmapToScale: Bitmap?,
        newWidth: Float,
        newHeight: Float
    ): Bitmap? {
        if (bitmapToScale == null) return null
        val width = bitmapToScale.width
        val height = bitmapToScale.height
        val matrix = Matrix()
        matrix.postScale(newWidth / width, newHeight / height)
        return Bitmap.createBitmap(
            bitmapToScale, 0, 0,
            bitmapToScale.width, bitmapToScale.height, matrix,
            true
        )
    }

    fun loadTexture(path: String): Any {
        var bitmap: Bitmap? = null
        val info = TextureInformation()
        try {
            var str = path
            if (!path.startsWith("/")) {
                str = "/$path"
            }
            val file = File(activity.getExternalFilesDir(null), str)
            if (file.canRead()) {
                bitmap = BitmapFactory.decodeStream(FileInputStream(file))
            } else {
                bitmap = BitmapFactory.decodeStream(
                    activity.resources.assets.open(path)
                )
            }
        } catch (e: Exception) {
            Log.w("NDKHelper", "Coundn't load a file:$path")
            info.ret = false
            return info
        }

        if (bitmap != null) {
            GLUtils.texImage2D(GL10.GL_TEXTURE_2D, 0, bitmap, 0)
        }
        info.ret = true
        info.alphaChannel = bitmap!!.hasAlpha()
        info.originalWidth = getBitmapWidth(bitmap)
        info.originalHeight = getBitmapHeight(bitmap)
        return info
    }

    fun loadCubemapTexture(path: String, face: Int, miplevel: Int, sRGB: Boolean): Any {
        var bitmap: Bitmap? = null
        val info = TextureInformation()
        try {
            var str = path
            if (!path.startsWith("/")) {
                str = "/$path"
            }
            val file = File(activity.getExternalFilesDir(null), str)
            if (file.canRead()) {
                bitmap = BitmapFactory.decodeStream(FileInputStream(file))
            } else {
                bitmap = BitmapFactory.decodeStream(
                    activity.resources.assets.open(path)
                )
            }
        } catch (e: Exception) {
            Log.w("NDKHelper", "Coundn't load a file:$path")
            info.ret = false
            return info
        }

        if (bitmap != null) {
            if (sRGB) {
                GLUtils.texImage2D(face, miplevel, bitmap, 0)
            } else
                GLUtils.texImage2D(face, miplevel, bitmap, 0)
        }
        info.ret = true
        info.alphaChannel = bitmap!!.hasAlpha()
        info.originalWidth = getBitmapWidth(bitmap)
        info.originalHeight = getBitmapHeight(bitmap)
        return info
    }

    fun loadImage(path: String): Any {
        var bitmap: Bitmap? = null
        val info = TextureInformation()
        try {
            var str = path
            if (!path.startsWith("/")) {
                str = "/$path"
            }
            val file = File(activity.getExternalFilesDir(null), str)
            if (file.canRead()) {
                bitmap = BitmapFactory.decodeStream(FileInputStream(file))
            } else {
                bitmap = BitmapFactory.decodeStream(
                    activity.resources.assets.open(path)
                )
            }
        } catch (e: Exception) {
            Log.w("NDKHelper", "Coundn't load a file:$path")
            info.ret = false
            return info
        }

        if (bitmap != null) {
            GLUtils.texImage2D(GL10.GL_TEXTURE_2D, 0, bitmap, 0)
        }
        info.ret = true
        info.alphaChannel = bitmap!!.hasAlpha()
        info.originalWidth = getBitmapWidth(bitmap)
        info.originalHeight = getBitmapHeight(bitmap)

        val iBytes = bitmap.width * bitmap.height * 4
        val buffer = ByteBuffer.allocateDirect(iBytes)
        bitmap.copyPixelsToBuffer(buffer)
        info.image = buffer
        return info
    }

    fun openBitmap(path: String, iScalePOT: Boolean): Bitmap? {
        var bitmap: Bitmap? = null
        try {
            bitmap = BitmapFactory.decodeStream(
                activity.resources.assets.open(path)
            )
            if (iScalePOT) {
                val originalWidth = getBitmapWidth(bitmap!!)
                val originalHeight = getBitmapHeight(bitmap)
                val width = nextPOT(originalWidth)
                val height = nextPOT(originalHeight)
                if (originalWidth != width || originalHeight != height) {
                    bitmap = scaleBitmap(bitmap, width.toFloat(), height.toFloat())
                }
            }
        } catch (e: Exception) {
            Log.w("NDKHelper", "Coundn't load a file:$path")
        }
        return bitmap
    }

    fun getBitmapWidth(bmp: Bitmap): Int = bmp.width
    fun getBitmapHeight(bmp: Bitmap): Int = bmp.height

    fun getBitmapPixels(bmp: Bitmap, pixels: IntArray) {
        val w = bmp.width
        val h = bmp.height
        bmp.getPixels(pixels, 0, w, 0, 0, w, h)
    }

    fun closeBitmap(bmp: Bitmap) {
        bmp.recycle()
    }

    fun getNativeLibraryDirectory(appContext: Context): String {
        val ai = activity.applicationInfo
        Log.w("NDKHelper", "ai.nativeLibraryDir:" + ai.nativeLibraryDir)
        if ((ai.flags and ApplicationInfo.FLAG_UPDATED_SYSTEM_APP) != 0
            || (ai.flags and ApplicationInfo.FLAG_SYSTEM) == 0
        ) {
            return ai.nativeLibraryDir
        }
        return "/system/lib/"
    }

    fun getApplicationName(): String {
        val pm = activity.packageManager
        val ai = try {
            pm.getApplicationInfo(activity.packageName, 0)
        } catch (e: NameNotFoundException) {
            null
        }
        return if (ai != null) pm.getApplicationLabel(ai) as String else "(unknown)"
    }

    fun getStringResource(resourceName: String): String {
        val id = activity.resources.getIdentifier(resourceName, "string", activity.packageName)
        return if (id == 0) "" else activity.resources.getText(id) as String
    }

    @TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR1)
    fun getNativeAudioBufferSize(): Int {
        val SDK_INT = Build.VERSION.SDK_INT
        if (SDK_INT >= 17) {
            val am = activity.getSystemService(Context.AUDIO_SERVICE) as AudioManager
            val framesPerBuffer = am.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)
            return framesPerBuffer.toInt()
        } else {
            return 0
        }
    }

    fun getNativeAudioSampleRate(): Int {
        return AudioTrack.getNativeOutputSampleRate(AudioManager.STREAM_SYSTEM)
    }

    fun runOnUIThread(p: Long) {
        if (checkSOLoaded()) {
            activity.runOnUiThread {
                RunOnUiThreadHandler(p)
            }
        }
        return
    }

    external fun RunOnUiThreadHandler(pointer: Long)

    companion object {
        private var loadedSO = false

        fun checkSOLoaded(): Boolean {
            if (!loadedSO) {
                Log.e(
                    "NDKHelper",
                    "--------------------------------------------\n"
                            + ".so has not been loaded. To use JUI helper, please initialize with \n"
                            + "NDKHelper::Init( ANativeActivity* activity, const char* helper_class_name, const char* native_soname);\n"
                            + "--------------------------------------------\n"
                )
                return false
            } else
                return true
        }
    }
}
