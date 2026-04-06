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

object MidiSpec {
    // Message Codes
    const val MIDICODE_NOTEOFF: Byte = 0x08
    const val MIDICODE_NOTEON: Byte = 0x09
    const val MIDICODE_POLYPRESS: Byte = 0x0A
    const val MIDICODE_CONTROLLER: Byte = 0x0B
    const val MIDICODE_PROGCHANGE: Byte = 0x0C
    const val MIDICODE_CHANPRESS: Byte = 0x0D
    const val MIDICODE_PITCHBEND: Byte = 0x0E

    // System Commands
    val MIDICODE_SYSEX: Byte = 0xF0.toByte()
    val MIDICODE_ENDOFSYSEX: Byte = 0xF7.toByte()
    val MIDICODE_ACTIVESENSING: Byte = 0xFE.toByte()
    val MIDICODE_RESET: Byte = 0xFF.toByte()

    // Continuous Controllers
    const val MAX_CC_VALUE: Byte = 127
    const val MIDICC_MODWHEEL: Byte = 1

    const val MAX_PITCHBEND_VALUE: Int = 0x3FFF
    const val MID_PITCHBEND_VALUE: Int = 0x2000

    fun makeChanMessageCode(msg: Byte, chan: Byte): Byte {
        return ((msg.toInt() shl 4) or chan.toInt()).toByte()
    }
}
