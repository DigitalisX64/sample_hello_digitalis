package com.example.hellonarrowret

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellonarrowret.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeNarrowReturns()
    }

    external fun probeNarrowReturns(): String

    companion object {
        init {
            System.loadLibrary("hellonarrowret")
        }

        // Called from native code through JNIEnv::CallStatic<Type>MethodA, one per
        // narrow Java primitive, so each result type crosses the proxy boundary.
        @JvmStatic fun alwaysFalse(): Boolean = false
        @JvmStatic fun alwaysTrue(): Boolean = true
        @JvmStatic fun minusOneByte(): Byte = -1
        @JvmStatic fun minusOneShort(): Short = -1
        @JvmStatic fun maxChar(): Char = '￿'
    }
}
