package com.example.hellobf16

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.hellobf16.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeBf16()
        runBenchmarks()
    }

    /**
     * Timed workload: bulk FP32 -> BF16 narrowing, the conversion an inference
     * runtime runs over every activation tensor before a BF16 kernel sees it.
     */
    private fun runBenchmarks() {
        val module = "hello-bf16"
        Bench.run(module, "bf16-narrow") { benchNarrowToBf16() }
        Bench.done(module)
    }

    external fun probeBf16(): String

    external fun benchNarrowToBf16(): Int

    companion object {
        init {
            System.loadLibrary("hellobf16")
        }
    }
}
