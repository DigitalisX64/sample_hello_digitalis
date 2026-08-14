package com.example.helloimagedecoder

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloimagedecoder.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeImageDecoder(cacheDir.absolutePath)
    }

    external fun probeImageDecoder(cacheDir: String): String

    companion object {
        init {
            System.loadLibrary("helloimagedecoder")
        }
    }
}
