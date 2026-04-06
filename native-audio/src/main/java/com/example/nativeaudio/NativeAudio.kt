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

package com.example.nativeaudio

import com.example.hellodigitalis.nativeaudio.BuildConfig
import com.example.hellodigitalis.nativeaudio.R

import android.Manifest
import android.annotation.TargetApi
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.content.res.AssetManager
import android.media.AudioManager
import android.os.Build
import android.os.Bundle
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.Toast
import androidx.core.app.ActivityCompat

class NativeAudio : Activity(),
    ActivityCompat.OnRequestPermissionsResultCallback {

    /** Called when the activity is first created. */
    @TargetApi(17)
    override fun onCreate(icicle: Bundle?) {
        super.onCreate(icicle)
        setContentView(R.layout.main)

        assetManager = assets

        // initialize native audio system
        createEngine()

        var sampleRate = 0
        var bufSize = 0
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
            val myAudioMgr = getSystemService(Context.AUDIO_SERVICE) as AudioManager
            var nativeParam = myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_SAMPLE_RATE)
            sampleRate = nativeParam.toInt()
            nativeParam = myAudioMgr.getProperty(AudioManager.PROPERTY_OUTPUT_FRAMES_PER_BUFFER)
            bufSize = nativeParam.toInt()
        }
        createBufferQueueAudioPlayer(sampleRate, bufSize)

        // initialize URI spinner
        val uriSpinner = findViewById<Spinner>(R.id.uri_spinner)
        val uriAdapter = ArrayAdapter.createFromResource(
            this, R.array.uri_spinner_array, android.R.layout.simple_spinner_item
        )
        uriAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
        uriSpinner.adapter = uriAdapter
        uriSpinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(parent: AdapterView<*>, view: View?, pos: Int, id: Long) {
                URI = parent.getItemAtPosition(pos).toString()
            }

            override fun onNothingSelected(parent: AdapterView<*>) {
                URI = null
            }
        }

        // initialize button click handlers

        findViewById<Button>(R.id.hello).setOnClickListener {
            selectClip(CLIP_HELLO, 5)
        }

        findViewById<Button>(R.id.android).setOnClickListener {
            selectClip(CLIP_ANDROID, 7)
        }

        findViewById<Button>(R.id.sawtooth).setOnClickListener {
            selectClip(CLIP_SAWTOOTH, 1)
        }

        findViewById<Button>(R.id.reverb).run {
            var enabled = false
            setOnClickListener {
                enabled = !enabled
                if (!enableReverb(enabled)) {
                    enabled = !enabled
                }
            }
        }

        findViewById<Button>(R.id.embedded_soundtrack).run {
            var created = false
            setOnClickListener {
                if (!created) {
                    created = createAssetAudioPlayer(assetManager!!, "background.mp3")
                }
                if (created) {
                    isPlayingAsset = !isPlayingAsset
                    setPlayingAssetAudioPlayer(isPlayingAsset)
                }
            }
        }

        findViewById<Button>(R.id.uri_soundtrack).run {
            var created = false
            setOnClickListener {
                if (!created && URI != null) {
                    created = createUriAudioPlayer(URI!!)
                }
            }
        }

        findViewById<Button>(R.id.pause_uri).setOnClickListener {
            setPlayingUriAudioPlayer(false)
        }

        findViewById<Button>(R.id.play_uri).setOnClickListener {
            setPlayingUriAudioPlayer(true)
        }

        findViewById<Button>(R.id.loop_uri).run {
            var isLooping = false
            setOnClickListener {
                isLooping = !isLooping
                setLoopingUriAudioPlayer(isLooping)
            }
        }

        findViewById<Button>(R.id.mute_left_uri).run {
            var muted = false
            setOnClickListener {
                muted = !muted
                setChannelMuteUriAudioPlayer(0, muted)
            }
        }

        findViewById<Button>(R.id.mute_right_uri).run {
            var muted = false
            setOnClickListener {
                muted = !muted
                setChannelMuteUriAudioPlayer(1, muted)
            }
        }

        findViewById<Button>(R.id.solo_left_uri).run {
            var soloed = false
            setOnClickListener {
                soloed = !soloed
                setChannelSoloUriAudioPlayer(0, soloed)
            }
        }

        findViewById<Button>(R.id.solo_right_uri).run {
            var soloed = false
            setOnClickListener {
                soloed = !soloed
                setChannelSoloUriAudioPlayer(1, soloed)
            }
        }

        findViewById<Button>(R.id.mute_uri).run {
            var muted = false
            setOnClickListener {
                muted = !muted
                setMuteUriAudioPlayer(muted)
            }
        }

        findViewById<Button>(R.id.enable_stereo_position_uri).run {
            var enabled = false
            setOnClickListener {
                enabled = !enabled
                enableStereoPositionUriAudioPlayer(enabled)
            }
        }

        findViewById<Button>(R.id.channels_uri).setOnClickListener {
            if (numChannelsUri == 0) {
                numChannelsUri = getNumChannelsUriAudioPlayer()
            }
            Toast.makeText(this@NativeAudio, "Channels: $numChannelsUri", Toast.LENGTH_SHORT).show()
        }

        findViewById<SeekBar>(R.id.volume_uri).setOnSeekBarChangeListener(
            object : SeekBar.OnSeekBarChangeListener {
                var lastProgress = 100
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    if (BuildConfig.DEBUG && !(progress in 0..100)) {
                        throw AssertionError()
                    }
                    lastProgress = progress
                }

                override fun onStartTrackingTouch(seekBar: SeekBar) {}
                override fun onStopTrackingTouch(seekBar: SeekBar) {
                    val attenuation = 100 - lastProgress
                    val millibel = attenuation * -50
                    setVolumeUriAudioPlayer(millibel)
                }
            })

        findViewById<SeekBar>(R.id.pan_uri).setOnSeekBarChangeListener(
            object : SeekBar.OnSeekBarChangeListener {
                var lastProgress = 100
                override fun onProgressChanged(seekBar: SeekBar, progress: Int, fromUser: Boolean) {
                    if (BuildConfig.DEBUG && !(progress in 0..100)) {
                        throw AssertionError()
                    }
                    lastProgress = progress
                }

                override fun onStartTrackingTouch(seekBar: SeekBar) {}
                override fun onStopTrackingTouch(seekBar: SeekBar) {
                    val permille = (lastProgress - 50) * 20
                    setStereoPositionUriAudioPlayer(permille)
                }
            })

        if (Build.VERSION.SDK_INT > 19) {
            val uriIds = intArrayOf(
                R.id.uri_soundtrack, R.id.pause_uri,
                R.id.play_uri, R.id.loop_uri,
                R.id.mute_left_uri, R.id.mute_right_uri,
                R.id.solo_left_uri, R.id.solo_right_uri,
                R.id.mute_uri, R.id.enable_stereo_position_uri,
                R.id.channels_uri, R.id.volume_uri,
                R.id.pan_uri, R.id.uri_spinner
            )
            for (id in uriIds)
                findViewById<View>(id).isEnabled = false
        }

        findViewById<Button>(R.id.record).setOnClickListener {
            val status = ActivityCompat.checkSelfPermission(
                this@NativeAudio,
                Manifest.permission.RECORD_AUDIO
            )
            if (status != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(
                    this@NativeAudio,
                    arrayOf(Manifest.permission.RECORD_AUDIO),
                    AUDIO_ECHO_REQUEST
                )
                return@setOnClickListener
            }
            recordAudio()
        }

        findViewById<Button>(R.id.playback).setOnClickListener {
            selectClip(CLIP_PLAYBACK, 3)
        }
    }

    private fun recordAudio() {
        if (!created) {
            created = createAudioRecorder()
        }
        if (created) {
            startRecording()
        }
    }

    override fun onPause() {
        selectClip(CLIP_NONE, 0)
        isPlayingAsset = false
        setPlayingAssetAudioPlayer(false)
        isPlayingUri = false
        setPlayingUriAudioPlayer(false)
        super.onPause()
    }

    override fun onDestroy() {
        shutdown()
        super.onDestroy()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<String>,
        grantResults: IntArray
    ) {
        if (AUDIO_ECHO_REQUEST != requestCode) {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults)
            return
        }

        if (grantResults.size != 1 ||
            grantResults[0] != PackageManager.PERMISSION_GRANTED
        ) {
            Toast.makeText(
                applicationContext,
                getString(R.string.NeedRecordAudioPermission),
                Toast.LENGTH_SHORT
            ).show()
            return
        }

        recordAudio()
    }

    companion object {
        private const val AUDIO_ECHO_REQUEST = 0

        const val CLIP_NONE = 0
        const val CLIP_HELLO = 1
        const val CLIP_ANDROID = 2
        const val CLIP_SAWTOOTH = 3
        const val CLIP_PLAYBACK = 4

        @JvmStatic
        var URI: String? = null
        @JvmStatic
        var assetManager: AssetManager? = null

        @JvmStatic
        var isPlayingAsset = false
        @JvmStatic
        var isPlayingUri = false

        @JvmStatic
        var numChannelsUri = 0

        var created = false

        @JvmStatic
        external fun createEngine()
        @JvmStatic
        external fun createBufferQueueAudioPlayer(sampleRate: Int, samplesPerBuf: Int)
        @JvmStatic
        external fun createAssetAudioPlayer(assetManager: AssetManager, filename: String): Boolean
        @JvmStatic
        external fun setPlayingAssetAudioPlayer(isPlaying: Boolean)
        @JvmStatic
        external fun createUriAudioPlayer(uri: String): Boolean
        @JvmStatic
        external fun setPlayingUriAudioPlayer(isPlaying: Boolean)
        @JvmStatic
        external fun setLoopingUriAudioPlayer(isLooping: Boolean)
        @JvmStatic
        external fun setChannelMuteUriAudioPlayer(chan: Int, mute: Boolean)
        @JvmStatic
        external fun setChannelSoloUriAudioPlayer(chan: Int, solo: Boolean)
        @JvmStatic
        external fun getNumChannelsUriAudioPlayer(): Int
        @JvmStatic
        external fun setVolumeUriAudioPlayer(millibel: Int)
        @JvmStatic
        external fun setMuteUriAudioPlayer(mute: Boolean)
        @JvmStatic
        external fun enableStereoPositionUriAudioPlayer(enable: Boolean)
        @JvmStatic
        external fun setStereoPositionUriAudioPlayer(permille: Int)
        @JvmStatic
        external fun selectClip(which: Int, count: Int): Boolean
        @JvmStatic
        external fun enableReverb(enabled: Boolean): Boolean
        @JvmStatic
        external fun createAudioRecorder(): Boolean
        @JvmStatic
        external fun startRecording()
        @JvmStatic
        external fun shutdown()

        init {
            System.loadLibrary("native-audio-jni")
        }
    }
}
