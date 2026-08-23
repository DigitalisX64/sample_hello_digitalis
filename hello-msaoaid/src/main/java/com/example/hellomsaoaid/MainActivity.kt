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
package com.example.hellomsaoaid

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellomsaoaid.R

/**
 * Isolated reproducer for the MSA OAID security library (libmsaoaidsec.so),
 * the "移动安全联盟" device-id SDK's native component that many real apps bundle
 * (e.g. NetEase Cloud Music).
 *
 * All of libmsaoaidsec's interesting behaviour runs from its ELF constructors
 * (DT_INIT_ARRAY) at dlopen time — no JNI entry point is needed. One of those
 * constructors is an anti-tamper pass that resolves the dynamic linker's
 * internal `solist` symbol and walks the loaded-soinfo list. Under Berberis it
 * resolves the guest ARM64 linker's `solist_head` OFFSET (via the redirected
 * open of /system/bin/linker64) but adds the wrong linker's load BASE, so it
 * reads host x86_64 linker machine code as the list head and dereferences it,
 * crashing with SIGSEGV. So merely System.loadLibrary("msaoaidsec") reproduces
 * the fault in isolation, without NetEase, ShadowHook, or the OAID Java SDK.
 *
 * Logs "MSAOAID OK" if the library loads and its constructors complete, or
 * "MSAOAID FAIL: <reason>" on a Java-visible failure. A native SIGSEGV in a
 * constructor takes the process down before either marker is logged, which is
 * exactly the translator bug this sample pins.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runProbe()
    }

    private fun runProbe(): String {
        val msg = try {
            // Constructors (incl. the anti-tamper solist walk) run here.
            System.loadLibrary("msaoaidsec")
            "MSAOAID OK: libmsaoaidsec loaded, constructors completed"
        } catch (t: Throwable) {
            "MSAOAID FAIL: ${t.javaClass.simpleName}: ${t.message}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloMsaOaid"
    }
}
