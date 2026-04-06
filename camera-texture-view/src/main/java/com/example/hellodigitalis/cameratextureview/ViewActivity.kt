/*
 * Copyright (C) 2017 The Android Open Source Project
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

package com.example.hellodigitalis.cameratextureview

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Matrix
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata.LENS_FACING_BACK
import android.os.Bundle
import android.util.Log
import android.util.Size
import android.view.Gravity
import android.view.Surface
import android.view.TextureView
import android.view.View
import android.widget.FrameLayout
import androidx.core.app.ActivityCompat

class ViewActivity : Activity(),
    TextureView.SurfaceTextureListener,
    ActivityCompat.OnRequestPermissionsResultCallback {

    var ndkCamera_: Long = 0
    private var textureView_: TextureView? = null
    var surface_: Surface? = null
    private var cameraPreviewSize_: Size? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        onWindowFocusChanged(true)
        setContentView(R.layout.activity_main)
        if (isCamera2Device()) {
            RequestCamera()
        } else {
            Log.e("CameraSample", "Found legacy camera device, this sample needs camera2 device")
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
        }
    }

    private fun isCamera2Device(): Boolean {
        val camMgr = getSystemService(Context.CAMERA_SERVICE) as CameraManager
        var camera2Dev = true
        try {
            val cameraIds = camMgr.cameraIdList
            if (cameraIds.isNotEmpty()) {
                for (id in cameraIds) {
                    val characteristics = camMgr.getCameraCharacteristics(id)
                    val deviceLevel = characteristics.get(CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL)!!
                    val facing = characteristics.get(CameraCharacteristics.LENS_FACING)!!
                    if (deviceLevel == CameraCharacteristics.INFO_SUPPORTED_HARDWARE_LEVEL_LEGACY &&
                        facing == LENS_FACING_BACK
                    ) {
                        camera2Dev = false
                    }
                }
            }
        } catch (e: CameraAccessException) {
            e.printStackTrace()
            camera2Dev = false
        }
        return camera2Dev
    }

    private fun createTextureView() {
        textureView_ = findViewById<TextureView>(R.id.texturePreview)
        textureView_!!.surfaceTextureListener = this
        if (textureView_!!.isAvailable) {
            onSurfaceTextureAvailable(
                textureView_!!.surfaceTexture!!,
                textureView_!!.width, textureView_!!.height
            )
        }
    }

    override fun onSurfaceTextureAvailable(
        surface: SurfaceTexture,
        width: Int,
        height: Int
    ) {
        createNativeCamera()

        resizeTextureView(width, height)
        surface.setDefaultBufferSize(
            cameraPreviewSize_!!.width,
            cameraPreviewSize_!!.height
        )
        surface_ = Surface(surface)
        onPreviewSurfaceCreated(ndkCamera_, surface_!!)
    }

    private fun resizeTextureView(textureWidth: Int, textureHeight: Int) {
        val rotation = windowManager.defaultDisplay.rotation
        val newWidth = textureWidth
        var newHeight = textureWidth * cameraPreviewSize_!!.width / cameraPreviewSize_!!.height

        if (Surface.ROTATION_90 == rotation || Surface.ROTATION_270 == rotation) {
            newHeight = (textureWidth * cameraPreviewSize_!!.height) / cameraPreviewSize_!!.width
        }
        textureView_!!.layoutParams =
            FrameLayout.LayoutParams(newWidth, newHeight, Gravity.CENTER)
        configureTransform(newWidth, newHeight)
    }

    fun configureTransform(width: Int, height: Int) {
        val mDisplayOrientation = windowManager.defaultDisplay.rotation * 90
        val matrix = Matrix()
        if (mDisplayOrientation % 180 == 90) {
            matrix.setPolyToPoly(
                floatArrayOf(
                    0f, 0f,
                    width.toFloat(), 0f,
                    0f, height.toFloat(),
                    width.toFloat(), height.toFloat(),
                ), 0,
                if (mDisplayOrientation == 90)
                    floatArrayOf(
                        0f, height.toFloat(),
                        0f, 0f,
                        width.toFloat(), height.toFloat(),
                        width.toFloat(), 0f,
                    )
                else
                    floatArrayOf(
                        width.toFloat(), 0f,
                        width.toFloat(), height.toFloat(),
                        0f, 0f,
                        0f, height.toFloat(),
                    ), 0,
                4
            )
        } else if (mDisplayOrientation == 180) {
            matrix.postRotate(180f, width / 2f, height / 2f)
        }
        textureView_!!.setTransform(matrix)
    }

    override fun onSurfaceTextureSizeChanged(
        surface: SurfaceTexture,
        width: Int,
        height: Int
    ) {
    }

    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
        onPreviewSurfaceDestroyed(ndkCamera_, surface_!!)
        deleteCamera(ndkCamera_, surface_!!)
        ndkCamera_ = 0
        surface_ = null
        return true
    }

    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
    }

    fun RequestCamera() {
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.CAMERA) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            ActivityCompat.requestPermissions(
                this,
                arrayOf(Manifest.permission.CAMERA),
                PERMISSION_REQUEST_CODE_CAMERA
            )
            return
        }
        createTextureView()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        grantResults: IntArray
    ) {
        if (PERMISSION_REQUEST_CODE_CAMERA != requestCode) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults)
            return
        }

        if (grantResults.size == 1 &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED
        ) {
            val initCamera = Thread {
                runOnUiThread {
                    createTextureView()
                }
            }
            initCamera.start()
        }
    }

    private fun createNativeCamera() {
        val display = windowManager.defaultDisplay
        val height = display.mode.physicalHeight
        val width = display.mode.physicalWidth

        ndkCamera_ = createCamera(width, height)
        cameraPreviewSize_ = getMinimumCompatiblePreviewSize(ndkCamera_)
    }

    private external fun createCamera(width: Int, height: Int): Long
    private external fun getMinimumCompatiblePreviewSize(ndkCamera: Long): Size
    private external fun onPreviewSurfaceCreated(ndkCamera: Long, surface: Surface)
    private external fun onPreviewSurfaceDestroyed(ndkCamera: Long, surface: Surface)
    private external fun deleteCamera(ndkCamera: Long, surface: Surface)

    companion object {
        private const val PERMISSION_REQUEST_CODE_CAMERA = 1

        init {
            System.loadLibrary("camera_textureview")
        }
    }
}
