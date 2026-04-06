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

import android.media.midi.MidiDevice
import android.media.midi.MidiDeviceInfo
import android.media.midi.MidiManager
import android.media.midi.MidiInputPort

class AppMidiManager(private val mMidiManager: MidiManager) {

    // Selected Device(s)
    private var mReceiveDevice: MidiDevice? = null // an "Output" device is one we will RECEIVE data FROM

    private var mSendDevice: MidiDevice? = null // an "Input" device is one we will SEND data TO
    private var mSendPort: MidiInputPort? = null

    private val mUseRunningStatus = true

    fun GetMidiManager(): MidiManager {
        return mMidiManager
    }

    /**
     * Scan attached Midi devices forcefully from scratch
     */
    fun ScanMidiDevices(
        sendDevices: ArrayList<MidiDeviceInfo>,
        receiveDevices: ArrayList<MidiDeviceInfo>
    ) {
        sendDevices.clear()
        receiveDevices.clear()
        val devInfos = mMidiManager.devices
        for (devInfo in devInfos) {
            val numInPorts = devInfo.inputPortCount
            val deviceName = devInfo.properties.getString(MidiDeviceInfo.PROPERTY_NAME)
                ?: continue
            if (numInPorts > 0) {
                sendDevices.add(devInfo)
            }

            val numOutPorts = devInfo.outputPortCount
            if (numOutPorts > 0) {
                receiveDevices.add(devInfo)
            }
        }
    }

    //
    // Receive Device
    //
    inner class OpenMidiReceiveDeviceListener : MidiManager.OnDeviceOpenedListener {
        override fun onDeviceOpened(device: MidiDevice) {
            mReceiveDevice = device
            startReadingMidi(mReceiveDevice!!, 0/*mPortNumber*/)
        }
    }

    fun openReceiveDevice(devInfo: MidiDeviceInfo) {
        mMidiManager.openDevice(devInfo, OpenMidiReceiveDeviceListener(), null)
    }

    fun closeReceiveDevice() {
        if (mReceiveDevice != null) {
            // Native API
            mReceiveDevice = null
        }
    }

    //
    // Send Device
    //
    inner class OpenMidiSendDeviceListener : MidiManager.OnDeviceOpenedListener {
        override fun onDeviceOpened(device: MidiDevice) {
            mSendDevice = device
            startWritingMidi(mSendDevice!!, 0/*mPortNumber*/)
        }
    }

    fun openSendDevice(devInfo: MidiDeviceInfo) {
        mMidiManager.openDevice(devInfo, OpenMidiSendDeviceListener(), null)
    }

    fun closeSendDevice() {
        if (mSendDevice != null) {
            // Native API
            mSendDevice = null
        }
    }

    private fun sendMessages(msgBuff: ByteArray) {
        writeMidi(msgBuff, msgBuff.size)
    }

    //
    // Message Sending methods
    //
    fun sendNoteOn(chan: Byte, keys: ByteArray, velocities: ByteArray) {
        val keyMsgBuff = MidiDataHelper.make3ByteMsgBuff(
            MidiSpec.MIDICODE_NOTEON, chan, keys, velocities, mUseRunningStatus
        )
        sendMessages(keyMsgBuff)
    }

    fun sendNoteOff(chan: Byte, keys: ByteArray, velocities: ByteArray) {
        val keyMsgBuff = MidiDataHelper.make3ByteMsgBuff(
            MidiSpec.MIDICODE_NOTEOFF, chan, keys, velocities, mUseRunningStatus
        )
        sendMessages(keyMsgBuff)
    }

    fun sendController(chan: Byte, controller: Byte, value: Byte) {
        val controllers = byteArrayOf(controller)
        val values = byteArrayOf(value)
        val msgBuff = MidiDataHelper.make3ByteMsgBuff(
            MidiSpec.MIDICODE_CONTROLLER, chan, controllers, values, mUseRunningStatus
        )
        sendMessages(msgBuff)
    }

    fun sendPitchBend(chan: Byte, value: Int) {
        val lsbs = byteArrayOf((value and 0xEF).toByte())
        val msbs = byteArrayOf(((value shr 7) and 0xEF).toByte())
        val msgBuff = MidiDataHelper.make3ByteMsgBuff(
            MidiSpec.MIDICODE_PITCHBEND, chan, lsbs, msbs, mUseRunningStatus
        )
        sendMessages(msgBuff)
    }

    fun sendProgramChange(chan: Byte, value: Byte) {
        val values = byteArrayOf(value)
        val msgBuff = MidiDataHelper.make2ByteMsgBuff(
            MidiSpec.MIDICODE_PROGCHANGE, chan, values, mUseRunningStatus
        )
        sendMessages(msgBuff)
    }

    //
    // Native API stuff
    //
    external fun startReadingMidi(receiveDevice: MidiDevice, portNumber: Int)
    external fun stopReadingMidi()

    external fun startWritingMidi(sendDevice: MidiDevice, portNumber: Int)
    external fun stopWritingMidi()
    external fun writeMidi(data: ByteArray, length: Int)

    companion object {
        private val TAG = AppMidiManager::class.java.name

        fun loadNativeAPI() {
            System.loadLibrary("native_midi")
        }
    }
}
