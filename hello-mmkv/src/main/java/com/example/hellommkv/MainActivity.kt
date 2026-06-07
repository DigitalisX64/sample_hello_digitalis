package com.example.hellommkv

import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellommkv.R
import com.tencent.mmkv.MMKV

/**
 * Exercises Tencent MMKV — a native (C++) key-value store — under Berberis
 * ARM64->x86_64 translation. MMKV.initialize loads the arm64-v8a libmmkv.so;
 * the probe round-trips several value types through MMKV's mmap-backed native
 * storage and self-checks the results, logging "MMKV OK" or "MMKV FAIL" so the
 * suite's StatusTest can assert a clean run.
 */
class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = runMmkvProbe()
    }

    private fun runMmkvProbe(): String {
        val rootDir = MMKV.initialize(this)
        val kv = MMKV.mmkvWithID("hello-mmkv")
        // Start from a clean slate so re-runs are deterministic.
        kv.clearAll()

        kv.encode("int", 1234567)
        kv.encode("bool", true)
        kv.encode("long", 0x1122334455667788L)
        kv.encode("double", 3.14159265358979)
        kv.encode("str", "héllo-mmkv-ⓦ")
        kv.encode("bytes", byteArrayOf(0, 1, 2, 127, -1, -128))

        val checks = listOf(
            "int" to (kv.decodeInt("int") == 1234567),
            "bool" to kv.decodeBool("bool"),
            "long" to (kv.decodeLong("long") == 0x1122334455667788L),
            "double" to (kv.decodeDouble("double") == 3.14159265358979),
            "str" to (kv.decodeString("str") == "héllo-mmkv-ⓦ"),
            "bytes" to (kv.decodeBytes("bytes")
                ?.contentEquals(byteArrayOf(0, 1, 2, 127, -1, -128)) == true),
        )

        val failed = checks.filterNot { it.second }.map { it.first }
        val msg = if (failed.isEmpty()) {
            "MMKV OK (v${MMKV.version()}, rootDir=$rootDir)"
        } else {
            "MMKV FAIL: round-trip mismatch for ${failed.joinToString(",")}"
        }
        Log.i(TAG, msg)
        return msg
    }

    companion object {
        private const val TAG = "HelloMMKV"
    }
}
