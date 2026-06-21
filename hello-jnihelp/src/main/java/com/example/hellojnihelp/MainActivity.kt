package com.example.hellojnihelp

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellojnihelp.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeJniHelp()
    }

    // Probe entry: drives the 12 libnativehelper jni* trampolines and self-checks.
    external fun probeJniHelp(): String

    // Target for the jniRegisterNativeMethods probe — the native side rebinds
    // this method via jniRegisterNativeMethods to confirm that trampoline works.
    external fun nativeRegisteredProbe(): Int

    companion object {
        init {
            System.loadLibrary("hellojnihelp")
        }
    }
}
