package com.example.helloi8mmbf16

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloi8mmbf16.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeI8mmBf16()
    }

    external fun probeI8mmBf16(): String

    companion object {
        init {
            System.loadLibrary("helloi8mmbf16")
        }
    }
}
