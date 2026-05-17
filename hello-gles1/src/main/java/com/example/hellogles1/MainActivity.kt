package com.example.hellogles1

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellogles1.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeGlesV1()
    }

    external fun probeGlesV1(): String

    companion object {
        init {
            System.loadLibrary("hellogles1")
        }
    }
}
