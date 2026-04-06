/*
 * Copyright (C) 2019 The Android Open Source Project
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
 *
 */

package com.example.nativemidi

import com.example.hellodigitalis.nativemidi.R

import android.app.Activity
import android.content.Context
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.os.Bundle
import android.os.Handler
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.SeekBar
import android.widget.Spinner
import android.widget.TextView

/**
 * Application MainActivity handles UI and Midi device hotplug event from
 * native side.
 */
class MainActivity : Activity(),
    View.OnClickListener,
    SeekBar.OnSeekBarChangeListener,
    AdapterView.OnItemSelectedListener {

    private var mAppMidiManager: AppMidiManager? = null

    // Connected devices
    private val mReceiveDevices = ArrayList<MidiDeviceInfo>()
    private val mSendDevices = ArrayList<MidiDeviceInfo>()

    // Send Widgets
    lateinit var mOutputDevicesSpinner: Spinner

    lateinit var mControllerSB: SeekBar
    lateinit var mPitchBendSB: SeekBar

    lateinit var mProgNumberEdit: EditText

    // Receive Widgets
    lateinit var mInputDevicesSpinner: Spinner
    lateinit var mReceiveMessageTx: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        //
        // Init JNI for data receive callback
        //
        initNative()

        //
        // Setup UI
        //
        mOutputDevicesSpinner = findViewById<Spinner>(R.id.outputDevicesSpinner)
        mOutputDevicesSpinner.onItemSelectedListener = this

        (findViewById<Button>(R.id.keyDownBtn)).setOnClickListener(this)
        (findViewById<Button>(R.id.keyUpBtn)).setOnClickListener(this)
        (findViewById<Button>(R.id.progChangeBtn)).setOnClickListener(this)

        mControllerSB = findViewById<SeekBar>(R.id.controllerSeekBar)
        mControllerSB.max = MidiSpec.MAX_CC_VALUE.toInt()
        mControllerSB.setOnSeekBarChangeListener(this)

        mPitchBendSB = findViewById<SeekBar>(R.id.pitchBendSeekBar)
        mPitchBendSB.max = MidiSpec.MAX_PITCHBEND_VALUE
        mPitchBendSB.progress = MidiSpec.MID_PITCHBEND_VALUE
        mPitchBendSB.setOnSeekBarChangeListener(this)

        mInputDevicesSpinner = findViewById<Spinner>(R.id.inputDevicesSpinner)
        mInputDevicesSpinner.onItemSelectedListener = this

        mProgNumberEdit = findViewById<EditText>(R.id.progNumEdit)

        mReceiveMessageTx = findViewById<TextView>(R.id.receiveMessageTx)

        val midiManager = getSystemService(Context.MIDI_SERVICE) as MidiManager
        midiManager.registerDeviceCallback(MidiDeviceCallback(), Handler())

        //
        // Setup the MIDI interface
        //
        mAppMidiManager = AppMidiManager(midiManager)

        // Initial Scan
        ScanMidiDevices()
    }

    /**
     * Device Scanning
     * Methods are called by the system whenever the set of attached devices changes.
     */
    private inner class MidiDeviceCallback : MidiManager.DeviceCallback() {
        override fun onDeviceAdded(device: MidiDeviceInfo) {
            ScanMidiDevices()
        }

        override fun onDeviceRemoved(device: MidiDeviceInfo) {
            ScanMidiDevices()
        }
    }

    /**
     * Scans and gathers the list of connected physical devices,
     * then calls onDeviceListChange() to update the UI.
     */
    private fun ScanMidiDevices() {
        mAppMidiManager!!.ScanMidiDevices(mSendDevices, mReceiveDevices)
        onDeviceListChange()
    }

    //
    // UI Helpers
    //
    /**
     * Formats a set of MIDI message bytes into a user-readable form.
     * @param message   The bytes comprising a Midi message.
     */
    private fun showReceivedMessage(message: ByteArray) {
        when ((message[0].toInt() and 0xF0) shr 4) {
            MidiSpec.MIDICODE_NOTEON.toInt() ->
                mReceiveMessageTx.text =
                    "NOTE_ON [ch:" + (message[0].toInt() and 0x0F) +
                            " key:" + message[1] +
                            " vel:" + message[2] + "]"

            MidiSpec.MIDICODE_NOTEOFF.toInt() ->
                mReceiveMessageTx.text =
                    "NOTE_OFF [ch:" + (message[0].toInt() and 0x0F) +
                            " key:" + message[1] +
                            " vel:" + message[2] + "]"

            // Potentially handle other messages here.
        }
    }

    //
    // View.OnClickListener overridden methods
    //
    override fun onClick(view: View) {
        val keys = byteArrayOf(60, 64, 67)         // C Major chord
        val velocities = byteArrayOf(60, 60, 60)   // Middling velocity
        val channel: Byte = 0    // send on channel 0
        val viewId = view.id
        if (viewId == R.id.keyDownBtn) {
            // Simulate a key-down
            mAppMidiManager!!.sendNoteOn(channel, keys, velocities)
        } else if (viewId == R.id.keyUpBtn) {
            // Simulate a key-up (converse of key-down above).
            mAppMidiManager!!.sendNoteOff(channel, keys, velocities)
        } else if (viewId == R.id.progChangeBtn) {
            // Send a MIDI program change message
            try {
                val progNumStr = mProgNumberEdit.text.toString()
                val progNum = progNumStr.toInt()
                mAppMidiManager!!.sendProgramChange(channel, progNum.toByte())
            } catch (ex: NumberFormatException) {
                // Maybe let the user know
            }
        }
    }

    //
    // SeekBar.OnSeekBarChangeListener overridden methods
    //
    override fun onProgressChanged(seekBar: SeekBar, pos: Int, fromUser: Boolean) {
        val seekBarId = seekBar.id
        if (seekBarId == R.id.controllerSeekBar) {
            mAppMidiManager!!.sendController(0.toByte(), MidiSpec.MIDICC_MODWHEEL, pos.toByte())
        } else if (seekBarId == R.id.pitchBendSeekBar) {
            mAppMidiManager!!.sendPitchBend(0.toByte(), pos)
        }
    }

    override fun onStartTrackingTouch(seekBar: SeekBar) {}

    override fun onStopTrackingTouch(seekBar: SeekBar) {}

    //
    // AdapterView.OnItemSelectedListener overridden methods
    //
    override fun onItemSelected(spinner: AdapterView<*>, view: View?, position: Int, id: Long) {
        val spinnerId = spinner.id
        if (spinnerId == R.id.outputDevicesSpinner) {
            val listItem = spinner.getItemAtPosition(position) as MidiDeviceListItem
            mAppMidiManager!!.openReceiveDevice(listItem.deviceInfo)
        } else if (spinnerId == R.id.inputDevicesSpinner) {
            val listItem = spinner.getItemAtPosition(position) as MidiDeviceListItem
            mAppMidiManager!!.openSendDevice(listItem.deviceInfo)
        }
    }

    override fun onNothingSelected(adapterView: AdapterView<*>) {}

    /**
     * A class to hold MidiDevices in the list controls.
     */
    private inner class MidiDeviceListItem(val deviceInfo: MidiDeviceInfo) {
        override fun toString(): String {
            return deviceInfo.properties.getString(MidiDeviceInfo.PROPERTY_NAME) ?: ""
        }
    }

    /**
     * Fills the specified list control with a set of MidiDevices
     */
    private fun fillDeviceList(spinner: Spinner, devices: ArrayList<MidiDeviceInfo>) {
        val listItems = ArrayList<MidiDeviceListItem>()
        for (devInfo in devices) {
            listItems.add(MidiDeviceListItem(devInfo))
        }

        // Creating adapter for spinner
        val dataAdapter = ArrayAdapter(
            this,
            android.R.layout.simple_spinner_item,
            listItems
        )
        // Drop down layout style - list view with radio button
        dataAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)

        // attaching data adapter to spinner
        spinner.adapter = dataAdapter
    }

    /**
     * Fills the Input & Output UI device list with the current set of MidiDevices for each type.
     */
    private fun onDeviceListChange() {
        fillDeviceList(mOutputDevicesSpinner, mReceiveDevices)
        fillDeviceList(mInputDevicesSpinner, mSendDevices)
    }

    //
    // Native Interface methods
    //
    private external fun initNative()

    /**
     * Called from the native code when MIDI messages are received.
     */
    private fun onNativeMessageReceive(message: ByteArray) {
        // Messages are received on some other thread, so switch to the UI thread
        // before attempting to access the UI
        runOnUiThread {
            showReceivedMessage(message)
        }
    }

    companion object {
        private val TAG = MainActivity::class.java.name

        // Force to load the native library
        init {
            AppMidiManager.loadNativeAPI()
        }
    }
}
