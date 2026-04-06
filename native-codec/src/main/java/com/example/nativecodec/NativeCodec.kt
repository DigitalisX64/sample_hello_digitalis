/*
 * Copyright (C) 2014 The Android Open Source Project
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

package com.example.nativecodec

import com.example.hellodigitalis.nativecodec.R

import android.app.Activity
import android.content.res.AssetManager
import android.graphics.SurfaceTexture
import android.os.Bundle
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CompoundButton
import android.widget.RadioButton
import android.widget.Spinner

class NativeCodec : Activity() {

    var mSourceString: String? = null

    lateinit var mSurfaceView1: SurfaceView
    lateinit var mSurfaceHolder1: SurfaceHolder

    var mSelectedVideoSink: VideoSink? = null
    var mNativeCodecPlayerVideoSink: VideoSink? = null

    var mSurfaceHolder1VideoSink: SurfaceHolderVideoSink? = null
    var mGLView1VideoSink: GLViewVideoSink? = null

    var mCreated = false
    var mIsPlaying = false

    private lateinit var mGLView1: MyGLSurfaceView
    private lateinit var mRadio1: RadioButton
    private lateinit var mRadio2: RadioButton

    /** Called when the activity is first created. */
    override fun onCreate(icicle: Bundle?) {
        super.onCreate(icicle)
        setContentView(R.layout.main)

        mGLView1 = findViewById<MyGLSurfaceView>(R.id.glsurfaceview1)

        // set up the Surface 1 video sink
        mSurfaceView1 = findViewById<SurfaceView>(R.id.surfaceview1)
        mSurfaceHolder1 = mSurfaceView1.holder

        mSurfaceHolder1.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                Log.v(TAG, "surfaceChanged format=$format, width=$width, height=$height")
            }

            override fun surfaceCreated(holder: SurfaceHolder) {
                Log.v(TAG, "surfaceCreated")
                if (mRadio1.isChecked) {
                    setSurface(holder.surface)
                }
            }

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                Log.v(TAG, "surfaceDestroyed")
            }
        })

        // initialize content source spinner
        val sourceSpinner = findViewById<Spinner>(R.id.source_spinner)
        val sourceAdapter = ArrayAdapter.createFromResource(
            this, R.array.source_array, android.R.layout.simple_spinner_item
        )
        sourceAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        sourceSpinner.adapter = sourceAdapter
        sourceSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>, view: View?, pos: Int, id: Long) {
                mSourceString = parent.getItemAtPosition(pos).toString()
                Log.v(TAG, "onItemSelected $mSourceString")
            }

            override fun onNothingSelected(parent: AdapterView<*>) {
                Log.v(TAG, "onNothingSelected")
                mSourceString = null
            }
        }

        mRadio1 = findViewById<RadioButton>(R.id.radio1)
        mRadio2 = findViewById<RadioButton>(R.id.radio2)

        val checklistener = CompoundButton.OnCheckedChangeListener { buttonView, isChecked ->
            Log.i("@@@@", "oncheckedchanged")
            if (buttonView === mRadio1 && isChecked) {
                mRadio2.isChecked = false
            }
            if (buttonView === mRadio2 && isChecked) {
                mRadio1.isChecked = false
            }
            if (isChecked) {
                if (mRadio1.isChecked) {
                    if (mSurfaceHolder1VideoSink == null) {
                        mSurfaceHolder1VideoSink = SurfaceHolderVideoSink(mSurfaceHolder1)
                    }
                    mSelectedVideoSink = mSurfaceHolder1VideoSink
                    mGLView1.onPause()
                    Log.i("@@@@", "glview pause")
                } else {
                    mGLView1.onResume()
                    if (mGLView1VideoSink == null) {
                        mGLView1VideoSink = GLViewVideoSink(mGLView1)
                    }
                    mSelectedVideoSink = mGLView1VideoSink
                }
                switchSurface()
            }
        }
        mRadio1.setOnCheckedChangeListener(checklistener)
        mRadio2.setOnCheckedChangeListener(checklistener)
        mRadio2.toggle()

        mSurfaceView1.setOnClickListener { mRadio1.toggle() }
        mGLView1.setOnClickListener { mRadio2.toggle() }

        // native MediaPlayer start/pause
        findViewById<Button>(R.id.start_native).setOnClickListener {
            if (!mCreated) {
                if (mNativeCodecPlayerVideoSink == null) {
                    if (mSelectedVideoSink == null) {
                        return@setOnClickListener
                    }
                    mSelectedVideoSink!!.useAsSinkForNative()
                    mNativeCodecPlayerVideoSink = mSelectedVideoSink
                }
                if (mSourceString != null) {
                    mCreated = createStreamingMediaPlayer(
                        resources.assets,
                        mSourceString!!
                    )
                }
            }
            if (mCreated) {
                mIsPlaying = !mIsPlaying
                setPlayingStreamingMediaPlayer(mIsPlaying)
            }
        }

        // native MediaPlayer rewind
        findViewById<Button>(R.id.rewind_native).setOnClickListener {
            if (mNativeCodecPlayerVideoSink != null) {
                rewindStreamingMediaPlayer()
            }
        }
    }

    fun switchSurface() {
        if (mCreated && mNativeCodecPlayerVideoSink !== mSelectedVideoSink) {
            Log.i("@@@", "shutting down player")
            shutdown()
            mCreated = false
            mSelectedVideoSink!!.useAsSinkForNative()
            mNativeCodecPlayerVideoSink = mSelectedVideoSink
            if (mSourceString != null) {
                Log.i("@@@", "recreating player")
                mCreated = createStreamingMediaPlayer(resources.assets, mSourceString!!)
                mIsPlaying = false
            }
        }
    }

    override fun onPause() {
        mIsPlaying = false
        setPlayingStreamingMediaPlayer(false)
        mGLView1.onPause()
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        if (mRadio2.isChecked) {
            mGLView1.onResume()
        }
    }

    override fun onDestroy() {
        shutdown()
        mCreated = false
        super.onDestroy()
    }

    abstract class VideoSink {
        abstract fun setFixedSize(width: Int, height: Int)
        abstract fun useAsSinkForNative()
    }

    class SurfaceHolderVideoSink(private val mSurfaceHolder: SurfaceHolder) : VideoSink() {
        override fun setFixedSize(width: Int, height: Int) {
            mSurfaceHolder.setFixedSize(width, height)
        }

        override fun useAsSinkForNative() {
            val s = mSurfaceHolder.surface
            Log.i("@@@", "setting surface $s")
            setSurface(s)
        }
    }

    class GLViewVideoSink(private val mMyGLSurfaceView: MyGLSurfaceView) : VideoSink() {
        override fun setFixedSize(width: Int, height: Int) {}

        override fun useAsSinkForNative() {
            val st = mMyGLSurfaceView.getSurfaceTexture()
            val s = Surface(st)
            setSurface(s)
            s.release()
        }
    }

    companion object {
        val TAG = "NativeCodec"

        @JvmStatic
        external fun createEngine()
        @JvmStatic
        external fun createStreamingMediaPlayer(assetMgr: AssetManager, filename: String): Boolean
        @JvmStatic
        external fun setPlayingStreamingMediaPlayer(isPlaying: Boolean)
        @JvmStatic
        external fun shutdown()
        @JvmStatic
        external fun setSurface(surface: Surface)
        @JvmStatic
        external fun rewindStreamingMediaPlayer()

        init {
            System.loadLibrary("native-codec-jni")
        }
    }
}
