package com.example.helloopenmaxal

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloopenmaxal.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeOpenMAXAL()
    }

    external fun probeOpenMAXAL(): String

    companion object {
        init {
            System.loadLibrary("helloopenmaxal")
        }
    }
}
