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
package com.example.hellodigitalis.camerabasic

import android.Manifest
import android.annotation.SuppressLint
import android.app.NativeActivity
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CameraMetadata.LENS_FACING_BACK
import android.os.Bundle
import android.util.Log
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.ImageButton
import android.widget.PopupWindow
import android.widget.RelativeLayout
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import androidx.core.app.ActivityCompat

class CameraSeekBar {
    var _progress: Int = 0
    var _min: Long = 0
    var _max: Long = 0
    var _absVal: Long = 0
    var _seekBar: SeekBar? = null
    var _sliderPrompt: TextView? = null

    constructor() {
        _progress = 0
        _min = 0
        _max = 0
        _absVal = 0
    }

    constructor(seekBar: SeekBar, textView: TextView, min: Long, max: Long, value: Long) {
        _seekBar = seekBar
        _sliderPrompt = textView
        _min = min
        _max = max
        _absVal = value

        if (_min != _max) {
            _progress = ((_absVal - _min) * seekBar.max / (_max - _min)).toInt()
            seekBar.progress = _progress
            updateProgress(_progress)
        } else {
            _progress = 0
            seekBar.isEnabled = false
        }
    }

    fun isSupported(): Boolean {
        return _min != _max
    }

    fun updateProgress(progress: Int) {
        if (!isSupported()) return

        _progress = progress
        _absVal = (progress * (_max - _min)) / _seekBar!!.max + _min
        val value = (progress * (_seekBar!!.width - 2 * _seekBar!!.thumbOffset)) / _seekBar!!.max
        _sliderPrompt!!.text = "" + _absVal
        _sliderPrompt!!.x = _seekBar!!.x + value + _seekBar!!.thumbOffset / 2
    }

    fun getProgress(): Int {
        return _progress
    }

    fun updateAbsProgress(value: Long) {
        if (!isSupported()) return
        val progress = ((value - _min) * _seekBar!!.max / (_max - _min)).toInt()
        updateProgress(progress)
    }

    fun getAbsProgress(): Long {
        return _absVal
    }
}

class CameraActivity : NativeActivity(),
    ActivityCompat.OnRequestPermissionsResultCallback {

    @Volatile
    var _savedInstance: CameraActivity? = null
    var _popupWindow: PopupWindow? = null
    var _takePhoto: ImageButton? = null
    var _exposure: CameraSeekBar? = null
    var _sensitivity: CameraSeekBar? = null
    var _initParams: LongArray? = null

    private val DBG_TAG = "NDK-CAMERA-BASIC"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(DBG_TAG, "OnCreate()")
        // new initialization here... request for permission
        _savedInstance = this

        setImmersiveSticky()
        val decorView = window.decorView
        decorView.setOnSystemUiVisibilityChangeListener {
            setImmersiveSticky()
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
                        facing == LENS_FACING_BACK) {
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

    // get current rotation method
    fun getRotationDegree(): Int {
        return 90 * (getSystemService(WINDOW_SERVICE) as WindowManager)
            .defaultDisplay
            .rotation
    }

    override fun onResume() {
        super.onResume()
        setImmersiveSticky()
    }

    fun setImmersiveSticky() {
        val decorView = window.decorView
        decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE)
    }

    override fun onPause() {
        if (_popupWindow != null && _popupWindow!!.isShowing) {
            _popupWindow!!.dismiss()
            _popupWindow = null
        }
        super.onPause()
    }

    override fun onDestroy() {
        super.onDestroy()
    }

    fun RequestCamera() {
        if (!isCamera2Device()) {
            Log.e(DBG_TAG, "Found legacy camera Device, this sample needs camera2 device")
            return
        }
        if (ActivityCompat.checkSelfPermission(
                this,
                Manifest.permission.CAMERA
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            ActivityCompat.requestPermissions(
                this,
                arrayOf(Manifest.permission.CAMERA),
                PERMISSION_REQUEST_CODE_CAMERA
            )
            return
        }
        notifyCameraPermission(true)
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        grantResults: IntArray
    ) {
        if (requestCode != PERMISSION_REQUEST_CODE_CAMERA) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults)
            return
        }

        if (permissions.isEmpty()) {
            RequestCamera()
            return
        }

        val granted = grantResults.all { it == PackageManager.PERMISSION_GRANTED }
        if (!granted) {
            logDeniedPermissions(permissions, grantResults)
        }
        notifyCameraPermission(granted)
    }

    private fun logDeniedPermissions(
        requestedPermissions: Array<String>,
        grantResults: IntArray
    ) {
        require(requestedPermissions.size == grantResults.size) {
            String.format(
                "requestedPermissions.length (%d) != grantResults.length (%d)",
                requestedPermissions.size,
                grantResults.size
            )
        }

        for (i in requestedPermissions.indices) {
            if (grantResults[i] != PackageManager.PERMISSION_GRANTED) {
                Log.i(DBG_TAG, requestedPermissions[i] + " DENIED")
            }
        }
    }

    /**
     * params[] exposure and sensitivity init values in (min, max, curVa) tuple
     *   0: exposure min
     *   1: exposure max
     *   2: exposure val
     *   3: sensitivity min
     *   4: sensitivity max
     *   5: sensitivity val
     */
    @SuppressLint("InflateParams")
    fun EnableUI(params: LongArray) {
        _initParams = params.copyOf()

        runOnUiThread {
            try {
                if (_popupWindow != null) {
                    _popupWindow!!.dismiss()
                }
                val layoutInflater = getBaseContext()
                    .getSystemService(LAYOUT_INFLATER_SERVICE) as LayoutInflater
                val popupView = layoutInflater.inflate(R.layout.widgets, null)
                _popupWindow = PopupWindow(
                    popupView,
                    WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.WRAP_CONTENT
                )

                val mainLayout = RelativeLayout(_savedInstance)
                val layoutParams = ViewGroup.MarginLayoutParams(-1, -1)
                layoutParams.setMargins(0, 0, 0, 0)
                _savedInstance!!.setContentView(mainLayout, layoutParams)

                _popupWindow!!.showAtLocation(mainLayout, Gravity.BOTTOM or Gravity.START, 0, 0)
                _popupWindow!!.update()

                _takePhoto = popupView.findViewById<ImageButton>(R.id.takePhoto)
                _takePhoto!!.setOnClickListener { TakePhoto() }
                _takePhoto!!.isEnabled = true
                popupView.findViewById<View>(R.id.exposureLabel).isEnabled = true
                popupView.findViewById<View>(R.id.sensitivityLabel).isEnabled = true

                var seekBar = popupView.findViewById<SeekBar>(R.id.exposure_seekbar)
                _exposure = CameraSeekBar(
                    seekBar,
                    popupView.findViewById<TextView>(R.id.exposureVal),
                    _initParams!![0], _initParams!![1], _initParams!![2]
                )
                seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                        _exposure!!.updateProgress(progress)
                        OnExposureChanged(_exposure!!.getAbsProgress())
                    }
                    override fun onStartTrackingTouch(seekBar: SeekBar) {}
                    override fun onStopTrackingTouch(seekBar: SeekBar) {}
                })

                seekBar = popupView.findViewById<SeekBar>(R.id.sensitivity_seekbar)
                _sensitivity = CameraSeekBar(
                    seekBar,
                    popupView.findViewById<TextView>(R.id.sensitivityVal),
                    _initParams!![3], _initParams!![4], _initParams!![5]
                )
                seekBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                        _sensitivity!!.updateProgress(progress)
                        OnSensitivityChanged(_sensitivity!!.getAbsProgress())
                    }
                    override fun onStartTrackingTouch(seekBar: SeekBar) {}
                    override fun onStopTrackingTouch(seekBar: SeekBar) {}
                })
            } catch (e: WindowManager.BadTokenException) {
                Log.e(DBG_TAG, "UI Exception Happened: " + e.message)
            }
        }
    }

    fun OnPhotoTaken(fileName: String) {
        runOnUiThread {
            Toast.makeText(
                applicationContext,
                "Photo saved to $fileName", Toast.LENGTH_SHORT
            ).show()
        }
    }

    external fun notifyCameraPermission(granted: Boolean)
    external fun TakePhoto()
    external fun OnExposureChanged(exposure: Long)
    external fun OnSensitivityChanged(sensitivity: Long)

    companion object {
        private const val PERMISSION_REQUEST_CODE_CAMERA = 1

        init {
            System.loadLibrary("ndk_camera")
        }
    }
}
