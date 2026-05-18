package com.example.hellosuperpackregress

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellosuperpackregress.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeRegressions()
    }

    external fun probeRegressions(): String

    companion object {
        init {
            System.loadLibrary("hellosuperpackregress")
        }
    }
}
