package com.example.hellolibclibm

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellolibclibm.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text =
            probeLibcLibm(filesDir.absolutePath)
    }

    external fun probeLibcLibm(writableDir: String): String

    companion object {
        init {
            System.loadLibrary("hellolibclibm")
        }
    }
}
