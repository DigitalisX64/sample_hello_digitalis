package com.example.exceptions

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView
import com.example.hellodigitalis.exceptions.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContentView(R.layout.activity_main)

        val sampleText = findViewById<TextView>(R.id.sample_text)
        try {
            throwsException();
            sampleText.text = "No exception thrown";
        } catch (e: java.lang.RuntimeException) {
            sampleText.text = "RuntimeException caught. Message: \"" + e.message + "\"";
        }

    }

    external fun throwsException()

    companion object {
        // Used to load the 'exceptions' library on application startup.
        init {
            System.loadLibrary("exceptions")
        }
    }
}
