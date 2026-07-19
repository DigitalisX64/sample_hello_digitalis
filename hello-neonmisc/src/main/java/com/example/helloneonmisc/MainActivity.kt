package com.example.helloneonmisc

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.helloneonmisc.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeNeonmisc()
    }

    external fun probeNeonmisc(): String

    companion object {
        init {
            System.loadLibrary("helloneonmisc")
        }
    }
}
