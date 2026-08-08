package com.example.helloneon

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.helloneon.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeNeon()
        runBenchmarks()
    }

    /**
     * Timed workloads: the vector requantize step, once through the rounding
     * shift and once through the saturating one. These are the shifts a
     * quantized network runs between every pair of layers, so how well they
     * lower is worth tracking over time and not only whether they are correct.
     */
    private fun runBenchmarks() {
        val module = "hello-neon"
        Bench.run(module, "srshl-requant") { benchRequantRounding(4) }
        Bench.run(module, "sqrshl-requant") { benchRequantSaturating(3) }
        Bench.done(module)
    }

    external fun probeNeon(): String

    external fun benchRequantRounding(shift: Int): Int

    external fun benchRequantSaturating(shift: Int): Int

    companion object {
        init {
            System.loadLibrary("helloneon")
        }
    }
}
