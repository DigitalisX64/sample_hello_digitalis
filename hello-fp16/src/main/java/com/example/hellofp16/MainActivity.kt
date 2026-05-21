package com.example.hellofp16

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellofp16.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeFp16()
    }

    external fun probeFp16(): String

    companion object {
        init {
            System.loadLibrary("hellofp16")
        }
    }
}
