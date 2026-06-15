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
package com.example.hellolibtorrent4j

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibtorrent4j.R
import org.libtorrent4j.LibTorrent
import org.libtorrent4j.SessionManager
import org.libtorrent4j.Sha1Hash

/**
 * Exercises libtorrent4j — Java/JNI bindings over libtorrent with statically
 * linked Boost.Asio and OpenSSL — under Berberis ARM64->x86_64 translation.
 * The first call into the swig layer triggers System.loadLibrary("torrent4j"),
 * which loads the arm64-v8a libtorrent4j.so (~15 MB of native C++). The probe
 * then drives three native paths, all offline (no network):
 *
 *   1. Version queries — LibTorrent.version()/boostVersion()/opensslVersion()
 *      read version strings out of the native libtorrent/Boost/OpenSSL code,
 *      proving the native module initialized.
 *   2. SessionManager start()/isRunning()/stop() — spins up the native libtorrent
 *      session, which starts Boost.Asio I/O threads under translation, then
 *      tears it down cleanly.
 *   3. Sha1Hash parseHex()/toHex() — a deterministic round-trip through the
 *      native sha1_hash SWIG object, asserting the 40-char hex echoes back.
 *
 * Logs "LIBTORRENT OK" or "LIBTORRENT FAIL: <reason>" so the suite's StatusTest
 * can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // 1. Version queries — force native load and read versions out of
            //    the native libtorrent / Boost / OpenSSL code.
            val ltVersion = LibTorrent.version()
            val boostVersion = LibTorrent.boostVersion()
            val opensslVersion = LibTorrent.opensslVersion()
            val versionOk = ltVersion.isNotBlank() &&
                boostVersion.isNotBlank() &&
                opensslVersion.isNotBlank()

            // 2. SessionManager lifecycle — starts native Boost.Asio I/O
            //    threads, then stops them. Heavy native init under translation.
            val session = SessionManager()
            session.start()
            val running = session.isRunning
            session.stop()

            // 3. Deterministic native SHA1 hash round-trip: parse a known
            //    40-char hex string into the native sha1_hash object, then
            //    render it back to hex and confirm it echoes exactly.
            val knownHex = "0123456789abcdef0123456789abcdef01234567"
            val hash: Sha1Hash = Sha1Hash.parseHex(knownHex)
            val roundTrip = hash.toHex().lowercase()
            val hashOk = roundTrip == knownHex && !hash.isAllZeros

            if (versionOk && running && hashOk) {
                "LIBTORRENT OK (libtorrent=$ltVersion boost=$boostVersion " +
                    "openssl=$opensslVersion, session ran, sha1 round-trip ok)"
            } else {
                "LIBTORRENT FAIL: versionOk=$versionOk running=$running " +
                    "hashOk=$hashOk (lt=$ltVersion sha1=$roundTrip)"
            }
        } catch (t: Throwable) {
            "LIBTORRENT FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloLibtorrent4j"
    }
}
