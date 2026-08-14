package com.example.hellosharedmem

import android.os.Bundle
import android.os.SharedMemory
import android.util.Log
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellosharedmem.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        val nativeResult = nativeProbe()
        val jniResult = probeJavaInterop()
        findViewById<TextView>(R.id.sample_text).text = "$nativeResult\n\n$jniResult"
    }

    // Kotlin creates android.os.SharedMemory, native dups the fd with
    // ASharedMemory_dupFromJava and fills a pattern, Kotlin maps read-only and
    // verifies the bytes — guest-native -> host-Java -> guest-native round trip.
    private fun probeJavaInterop(): String {
        val size = 8192
        try {
            val shm = SharedMemory.create("digitalis-jni", size)
            try {
                val native = nativeFillFromJava(shm, size)
                if (!native.startsWith("OK")) {
                    return native // native side already logged the FAIL detail
                }
                val buf = shm.mapReadOnly()
                try {
                    for (i in 0 until size) {
                        val want = ((i * 31 + 5) and 0xFF).toByte()
                        val got = buf.get(i)
                        if (got != want) {
                            val m = "hellosharedmem FAIL at jni-kotlin-verify: [$i]=$got want $want"
                            Log.e(TAG, m)
                            return m
                        }
                    }
                } finally {
                    SharedMemory.unmap(buf)
                }
                val m = "jni interop OK: $native; $size bytes verified in Kotlin"
                Log.i(TAG, m)
                return m
            } finally {
                shm.close()
            }
        } catch (t: Throwable) {
            val m = "hellosharedmem FAIL at jni-interop: $t"
            Log.e(TAG, m)
            return m
        }
    }

    external fun nativeProbe(): String
    external fun nativeFillFromJava(sharedMemory: SharedMemory, size: Int): String

    companion object {
        private const val TAG = "hellosharedmem"

        init {
            System.loadLibrary("hellosharedmem")
        }
    }
}
