package com.example.hellomediandksource

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellomediandksource.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeMediaNdkSource(cacheDir.absolutePath)
    }

    external fun probeMediaNdkSource(cacheDir: String): String

    companion object {
        init {
            System.loadLibrary("hellomediandksource")
        }
    }
}
