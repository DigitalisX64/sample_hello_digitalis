package com.example.helloneonmisc

import android.os.Bundle
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.example.hellodigitalis.bench.Bench
import com.example.hellodigitalis.helloneonmisc.R

class MainActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.sample_text).text = probeNeonmisc()
        runBenchmarks()
    }

    /**
     * Timed workloads: reciprocal and inverse square root, each by estimate
     * plus two Newton-Raphson steps. This is the sequence that replaces a
     * divide in a renderer or a solver, and it is only worth using if the
     * estimate instructions lower well.
     */
    private fun runBenchmarks() {
        val module = "hello-neonmisc"
        Bench.run(module, "recip-newton") { benchReciprocalNewton() }
        Bench.run(module, "rsqrt-newton") { benchRsqrtNewton() }
        Bench.done(module)
    }

    external fun probeNeonmisc(): String

    external fun benchReciprocalNewton(): Float

    external fun benchRsqrtNewton(): Float

    companion object {
        init {
            System.loadLibrary("helloneonmisc")
        }
    }
}
