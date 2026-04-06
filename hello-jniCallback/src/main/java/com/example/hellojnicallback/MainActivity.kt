/*
 * Copyright (C) 2016 The Android Open Source Project
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
package com.example.hellojnicallback

import androidx.annotation.Keep
import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView

import com.example.hellodigitalis.hellojnicallback.R

class MainActivity : AppCompatActivity() {

    var hour = 0
    var minute = 0
    var second = 0
    lateinit var tickView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        tickView = findViewById<TextView>(R.id.tickView)
    }

    override fun onResume() {
        super.onResume()
        hour = 0
        minute = 0
        second = 0
        (findViewById<TextView>(R.id.hellojniMsg)).text = stringFromJNI()
        startTicks()
    }

    override fun onPause() {
        super.onPause()
        StopTicks()
    }

    /*
     * A function calling from JNI to update current timer
     */
    @Keep
    private fun updateTimer() {
        ++second
        if (second >= 60) {
            ++minute
            second -= 60
            if (minute >= 60) {
                ++hour
                minute -= 60
            }
        }
        runOnUiThread {
            val ticks = "$hour:$minute:$second"
            tickView.text = ticks
        }
    }

    external fun stringFromJNI(): String
    external fun startTicks()
    external fun StopTicks()

    companion object {
        init {
            System.loadLibrary("hello-jnicallback")
        }
    }
}
