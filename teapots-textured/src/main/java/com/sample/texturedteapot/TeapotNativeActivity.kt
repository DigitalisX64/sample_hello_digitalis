/*
 * Copyright 2020 The Android Open Source Project
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

package com.sample.texturedteapot

import com.example.hellodigitalis.teapotstextured.R

import android.annotation.SuppressLint
import android.annotation.TargetApi
import android.app.NativeActivity
import android.os.Bundle
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup.MarginLayoutParams
import android.view.WindowManager.LayoutParams
import android.widget.LinearLayout
import android.widget.PopupWindow
import android.widget.TextView

class TeapotNativeActivity : NativeActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        //Hide toolbar
        val SDK_INT = android.os.Build.VERSION.SDK_INT
        if (SDK_INT >= 19) {
            setImmersiveSticky()

            val decorView = window.decorView
            decorView.setOnSystemUiVisibilityChangeListener {
                setImmersiveSticky()
            }
        }
    }

    @TargetApi(19)
    override fun onResume() {
        super.onResume()

        //Hide toolbar
        val SDK_INT = android.os.Build.VERSION.SDK_INT
        if (SDK_INT in 11..13) {
            window.decorView.systemUiVisibility = View.STATUS_BAR_HIDDEN
        } else if (SDK_INT in 14..18) {
            window.decorView.systemUiVisibility =
                View.SYSTEM_UI_FLAG_FULLSCREEN or View.SYSTEM_UI_FLAG_LOW_PROFILE
        } else if (SDK_INT >= 19) {
            setImmersiveSticky()
        }
    }

    // Our popup window, you will call it from your C/C++ code later

    @TargetApi(19)
    fun setImmersiveSticky() {
        val decorView = window.decorView
        decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE)
    }

    var _activity: TeapotNativeActivity? = null
    var _popupWindow: PopupWindow? = null
    var _label: TextView? = null

    @SuppressLint("InflateParams")
    fun showUI() {
        if (_popupWindow != null)
            return

        _activity = this

        this.runOnUiThread {
            val layoutInflater = getBaseContext()
                .getSystemService(LAYOUT_INFLATER_SERVICE) as LayoutInflater
            val popupView = layoutInflater.inflate(R.layout.widgets, null)
            _popupWindow = PopupWindow(
                popupView,
                LayoutParams.WRAP_CONTENT,
                LayoutParams.WRAP_CONTENT
            )

            val mainLayout = LinearLayout(_activity)
            val params = MarginLayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT)
            params.setMargins(0, 0, 0, 0)
            _activity!!.setContentView(mainLayout, params)

            // Show our UI over NativeActivity window
            _popupWindow!!.showAtLocation(mainLayout, Gravity.TOP or Gravity.START, 10, 10)
            _popupWindow!!.update()

            _label = popupView.findViewById<TextView>(R.id.textViewFPS)
        }
    }

    override fun onPause() {
        super.onPause()
    }

    fun updateFPS(fFPS: Float) {
        if (_label == null)
            return

        _activity = this
        this.runOnUiThread {
            _label!!.text = String.format("%2.2f FPS", fFPS)
        }
    }
}
