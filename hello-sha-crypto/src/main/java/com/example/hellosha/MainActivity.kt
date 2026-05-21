package com.example.hellosha

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellosha.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeShaCrypto()
    }

    external fun probeShaCrypto(): String

    companion object {
        init {
            System.loadLibrary("hellosha")
        }
    }
}
