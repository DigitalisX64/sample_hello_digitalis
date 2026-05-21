package com.example.hellobarriers

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellobarriers.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeBarriers()
    }

    external fun probeBarriers(): String

    companion object {
        init {
            System.loadLibrary("hellobarriers")
        }
    }
}
