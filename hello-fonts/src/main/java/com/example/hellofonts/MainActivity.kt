package com.example.hellofonts

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellofonts.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeFonts()
    }

    external fun probeFonts(): String

    companion object {
        init {
            System.loadLibrary("hellofonts")
        }
    }
}
