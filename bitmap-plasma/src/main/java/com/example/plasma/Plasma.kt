/*
 * Copyright (C) 2010 The Android Open Source Project
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
package com.example.plasma

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Point
import android.os.Bundle
import android.view.View

class Plasma : Activity() {
    // Called when the activity is first created.
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val display = windowManager.defaultDisplay
        val displaySize = Point()
        display.getSize(displaySize)
        setContentView(PlasmaView(this, displaySize.x, displaySize.y))
    }

    companion object {
        // load our native library
        init {
            System.loadLibrary("plasma")
        }
    }
}

// Custom view for rendering plasma.
//
// Note: suppressing lint warning for ViewConstructor since it is
//       manually set from the activity and not used in any layout.
@SuppressLint("ViewConstructor")
class PlasmaView(context: Context, width: Int, height: Int) : View(context) {
    private var mBitmap: Bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.RGB_565)
    private val mStartTime: Long = System.currentTimeMillis()

    override fun onDraw(canvas: Canvas) {
        renderPlasma(mBitmap, System.currentTimeMillis() - mStartTime)
        canvas.drawBitmap(mBitmap, 0f, 0f, null)
        // force a redraw, with a different time-based pattern.
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        mBitmap = Bitmap.createBitmap(w, h, Bitmap.Config.RGB_565)
    }

    companion object {
        // implemented by libplasma.so
        @JvmStatic
        private external fun renderPlasma(bitmap: Bitmap, time_ms: Long)
    }
}
