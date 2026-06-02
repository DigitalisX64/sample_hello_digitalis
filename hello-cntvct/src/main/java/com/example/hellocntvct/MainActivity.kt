package com.example.hellocntvct

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.hellocntvct.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeCntvct()
    }

    external fun probeCntvct(): String

    companion object {
        init {
            System.loadLibrary("hellocntvct")
        }
    }
}
