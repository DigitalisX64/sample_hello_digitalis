package com.example.hellohardwarebuffer

import android.hardware.HardwareBuffer
import android.os.Bundle
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellohardwarebuffer.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val view = findViewById<TextView>(R.id.sample_text)
        view.text = "probing AHardwareBuffer..."
        // Off the main thread: the socket-transport section uses multi-second
        // timeouts on its failure path, which must surface as a timed FAIL in
        // logcat, not as an unresponsive activity.
        Thread {
            val native = probeHardwareBuffer()
            val interop = probeJavaInterop()
            runOnUiThread { view.text = "$native\n$interop" }
        }.start()
    }

    // Native allocates + fills a buffer, wraps it via AHardwareBuffer_toHardwareBuffer,
    // and hands the Java object across JNI; Java inspects it with the SDK getters and
    // passes it back down for AHardwareBuffer_fromHardwareBuffer + pixel verification.
    private fun probeJavaInterop(): String {
        val hb = createTestBuffer()
        if (hb == null) {
            Log.e(TAG, "FAIL at createTestBuffer: returned null")
            return "java-interop FAILED: createTestBuffer returned null"
        }
        // Dimensions must match kJavaW/kJavaH/RGBA in native-lib.cpp.
        var javaSide = ""
        if (hb.width != 48 || hb.height != 24 || hb.layers != 1 ||
            hb.format != HardwareBuffer.RGBA_8888
        ) {
            Log.e(
                TAG,
                "FAIL at HardwareBuffer getters: ${hb.width}x${hb.height} " +
                    "layers=${hb.layers} format=${hb.format} want 48x24 layers=1 format=1"
            )
            javaSide = " (java-side getters FAILED)"
        }
        val result = verifyTestBuffer(hb)
        hb.close()
        return result + javaSide
    }

    external fun probeHardwareBuffer(): String
    external fun createTestBuffer(): HardwareBuffer?
    external fun verifyTestBuffer(buffer: HardwareBuffer): String

    companion object {
        private const val TAG = "hellohardwarebuffer"

        init {
            System.loadLibrary("hellohardwarebuffer")
        }
    }
}
