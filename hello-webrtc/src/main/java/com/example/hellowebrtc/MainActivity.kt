/*
 * Copyright (C) 2026 utzcoz
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
package com.example.hellowebrtc

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellowebrtc.R
import org.webrtc.DataChannel
import org.webrtc.IceCandidate
import org.webrtc.MediaConstraints
import org.webrtc.MediaStream
import org.webrtc.PeerConnection
import org.webrtc.PeerConnectionFactory
import org.webrtc.SdpObserver
import org.webrtc.SessionDescription
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicReference

/**
 * Exercises WebRTC (webrtc-sdk's Android build of Google's libwebrtc) — a large
 * native (C++) real-time-communication stack — under Berberis ARM64->x86_64
 * translation. The first PeerConnectionFactory call loads the arm64-v8a
 * libjingle_peerconnection_so.so (PeerConnection, SDP/codec negotiation, SCTP
 * DataChannel, plus the bundled libsrtp/usrsctp/abseil-cpp/boringssl C++
 * runtime).
 *
 * The probe runs entirely headless — no camera, microphone, or network:
 *   1. PeerConnectionFactory.initialize(...) — native global init.
 *   2. build a PeerConnectionFactory (no audio/video device modules).
 *   3. create a PeerConnection with an empty ICE-server list and an Observer.
 *   4. add a "digitalis" DataChannel (drives the native SCTP m=application path).
 *   5. createOffer(...) and, in onCreateSuccess, capture the generated SDP.
 * It then self-checks that the SDP starts with "v=0" and contains
 * "m=application" (the DataChannel media section), logging "WEBRTC OK" or
 * "WEBRTC FAIL" so the suite's StatusTest can assert a clean run. SDP/codec
 * setup is a heavy translator exercise (large C++ codepaths, lots of SIMD/string
 * work in the codec and SCTP layers).
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        var factory: PeerConnectionFactory? = null
        var peerConnection: PeerConnection? = null
        var dataChannel: DataChannel? = null
        val msg = try {
            // 1. Native global init.
            PeerConnectionFactory.initialize(
                PeerConnectionFactory.InitializationOptions
                    .builder(applicationContext)
                    .createInitializationOptions()
            )

            // 2. A bare factory: no audio/video device modules (headless).
            factory = PeerConnectionFactory.builder().createPeerConnectionFactory()
                ?: error("createPeerConnectionFactory returned null")

            // 3. A PeerConnection with an empty ICE-server list and an Observer.
            val iceServers = emptyList<PeerConnection.IceServer>()
            val observer = object : PeerConnection.Observer {
                override fun onSignalingChange(s: PeerConnection.SignalingState?) {}
                override fun onIceConnectionChange(s: PeerConnection.IceConnectionState?) {}
                override fun onIceConnectionReceivingChange(receiving: Boolean) {}
                override fun onIceGatheringChange(s: PeerConnection.IceGatheringState?) {}
                override fun onIceCandidate(candidate: IceCandidate?) {}
                override fun onIceCandidatesRemoved(candidates: Array<out IceCandidate>?) {}
                override fun onAddStream(stream: MediaStream?) {}
                override fun onRemoveStream(stream: MediaStream?) {}
                override fun onDataChannel(channel: DataChannel?) {}
                override fun onRenegotiationNeeded() {}
            }
            peerConnection = factory.createPeerConnection(iceServers, observer)
                ?: error("createPeerConnection returned null")

            // 4. A DataChannel — drives the native SCTP m=application SDP path.
            dataChannel = peerConnection.createDataChannel("digitalis", DataChannel.Init())
                ?: error("createDataChannel returned null")

            // 5. Generate an SDP offer and wait for the native callback.
            val sdpRef = AtomicReference<String?>(null)
            val errRef = AtomicReference<String?>(null)
            val latch = CountDownLatch(1)
            val sdpObserver = object : SdpObserver {
                override fun onCreateSuccess(sessionDescription: SessionDescription?) {
                    sdpRef.set(sessionDescription?.description)
                    latch.countDown()
                }

                override fun onCreateFailure(error: String?) {
                    errRef.set(error)
                    latch.countDown()
                }

                override fun onSetSuccess() {}
                override fun onSetFailure(error: String?) {}
            }
            peerConnection.createOffer(sdpObserver, MediaConstraints())

            if (!latch.await(8, TimeUnit.SECONDS)) {
                error("createOffer timed out (no native SDP callback in 8s)")
            }
            val err = errRef.get()
            if (err != null) {
                error("createOffer failed: $err")
            }
            val sdp = sdpRef.get() ?: error("createOffer succeeded but SDP was null")

            val startsWithV0 = sdp.startsWith("v=0")
            val hasDataChannel = sdp.contains("m=application")

            if (startsWithV0 && hasDataChannel) {
                "WEBRTC OK (SDP offer ${sdp.length} bytes, m=application present)"
            } else {
                "WEBRTC FAIL: startsWithV0=$startsWithV0 hasDataChannel=$hasDataChannel " +
                    "(SDP ${sdp.length} bytes)"
            }
        } catch (t: Throwable) {
            "WEBRTC FAIL: ${t.javaClass.simpleName}: ${t.message}"
        } finally {
            try {
                dataChannel?.dispose()
            } catch (_: Throwable) {
            }
            try {
                peerConnection?.dispose()
            } catch (_: Throwable) {
            }
            try {
                factory?.dispose()
            } catch (_: Throwable) {
            }
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloWebrtc"
    }
}
