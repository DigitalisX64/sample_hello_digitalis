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

object MidiDataHelper {
    /**
     * Create a MIDI data stream, containing a set of 3-value messages (potentially using
     * Running Status) each of the specific message-code/channel with associated 2-byte parameter
     * pairs.
     */
    fun make3ByteMsgBuff(
        msgID: Byte, channel: Byte, param_a: ByteArray, param_b: ByteArray, useRunningStatus: Boolean
    ): ByteArray {
        assert(param_a.size == param_b.size)

        val numMsgs = param_a.size

        val msgBuff: ByteArray
        if (useRunningStatus) {
            msgBuff = ByteArray(1 + (numMsgs * 2))
            var byteOffset = 0
            msgBuff[byteOffset++] = MidiSpec.makeChanMessageCode(msgID, channel)
            for (index in 0 until numMsgs) {
                msgBuff[byteOffset++] = param_a[index]
                msgBuff[byteOffset++] = param_b[index]
            }
        } else {
            msgBuff = ByteArray(numMsgs * 3)
            var byteOffset = 0
            for (index in 0 until numMsgs) {
                msgBuff[byteOffset++] = MidiSpec.makeChanMessageCode(msgID, channel)
                msgBuff[byteOffset++] = param_a[index]
                msgBuff[byteOffset++] = param_b[index]
            }
        }
        return msgBuff
    }

    /**
     * Create a MIDI data stream, containing a set of 2-value messages (potentially using
     * Running Status) each of the specific message-code/channel with associated 1-byte parameter
     * pairs.
     */
    fun make2ByteMsgBuff(
        msgID: Byte, channel: Byte, param_a: ByteArray, useRunningStatus: Boolean
    ): ByteArray {
        val numMsgs = param_a.size

        val msgBuff: ByteArray
        if (useRunningStatus) {
            msgBuff = ByteArray(1 + numMsgs)
            var byteOffset = 0
            msgBuff[byteOffset++] = MidiSpec.makeChanMessageCode(msgID, channel)
            for (index in 0 until numMsgs) {
                msgBuff[byteOffset++] = param_a[index]
            }
        } else {
            msgBuff = ByteArray(numMsgs * 2)
            var byteOffset = 0
            for (index in 0 until numMsgs) {
                msgBuff[byteOffset++] = MidiSpec.makeChanMessageCode(msgID, channel)
                msgBuff[byteOffset++] = param_a[index]
            }
        }
        return msgBuff
    }
}
